#include <stdio.h>
#include <string.h>
#include <jsonwriter.h>

#ifdef INCLUDE_UTILS
#include "utils.c"
#else
unsigned int json_esc1(const unsigned char *s, unsigned int slen,
                       unsigned int *replacelen,
                       unsigned char replace[], // replace buff should be at least 8 bytes long
                       const unsigned char **new_s,
                       size_t max_output_size);
#endif

struct jsonwriter_output_buff {
# define JSONWRITER_OUTPUT_BUFF_SIZE 65536
  unsigned char *buff;
  size_t used;

  size_t (*write)(const void * restrict, size_t, size_t, void * restrict);
  void *write_arg;
};

#define JSONWRITER_MAX_NESTING 256
struct jsonwriter_data {
  struct jsonwriter_output_buff out;

  unsigned int depth;
  unsigned char *close_brackets; // length = JSONWRITER_MAX_NESTING
  int *counts; // at each level, the current number of items
  char tmp[128]; // number buffer

  struct jsonwriter_variant (*to_jsw_variant)(void *);
  void (*after_to_jsw_variant)(void *, struct jsonwriter_variant *);

  unsigned char just_wrote_key:1;
  unsigned char compact:1;
  unsigned char started:1;
  unsigned char dummy:5;
};

static inline void jsonwriter_output_buff_flush(struct jsonwriter_output_buff *b) {
  if(b->used)
    b->write(b->buff, b->used, 1, b->write_arg);
  b->used = 0;
}

#ifndef JSONWRITER_NO_BUFF
static inline size_t jsonwriter_output_buff_write(struct jsonwriter_output_buff *b, const unsigned char *s, size_t n) {
  if(n) {
    if(n + b->used > JSONWRITER_OUTPUT_BUFF_SIZE) {
      jsonwriter_output_buff_flush(b);
      if(n > JSONWRITER_OUTPUT_BUFF_SIZE) // n too big, so write directly
        b->write(s, n, 1, b->write_arg);
    }
    // n + used < buff size
    memcpy(b->buff + b->used, s, n);
    b->used += n;
  }
  return n;
}
#else

static inline size_t jsonwriter_output_buff_write(struct jsonwriter_output_buff *b, const unsigned char *s, size_t n) {
  b->write(s, n, 1, b->write_arg);
}

#endif

void jsonwriter_set_option(jsonwriter_handle data, enum jsonwriter_option opt) {
  switch(opt) {
  case jsonwriter_option_pretty:
    data->compact = 0;
    break;
  case jsonwriter_option_compact:
    data->compact = 1;
    break;
  }
}

static size_t fwrite2(const void * restrict p, size_t n, size_t size, void * restrict f) {
  return (size_t) fwrite(p, n, size, f);
}

jsonwriter_handle jsonwriter_new(FILE *f) {
  return jsonwriter_new_stream(fwrite2, f);
}


jsonwriter_handle jsonwriter_new_stream(size_t (*write)(const void * restrict, size_t, size_t, void * restrict),
                                        void *write_arg) {
  struct jsonwriter_data *data = calloc(1, sizeof(*data));
  if(data) {
    data->out.write = write;
    data->out.write_arg = write_arg;
    if(!(data->out.buff = malloc(JSONWRITER_OUTPUT_BUFF_SIZE))
       || !(data->close_brackets = malloc(JSONWRITER_MAX_NESTING * sizeof(*data->close_brackets)))
       || !(data->counts = calloc(JSONWRITER_MAX_NESTING, sizeof(*data->counts)))) {
      // avoid jsonwriter_delete() delete here to suppress compiler warning
      free(data->counts);
      free(data->close_brackets);
      free(data->out.buff);
      free(data);
      data = NULL;
    }
  }
  return data;
}

void jsonwriter_flush(jsonwriter_handle data) {
  if(data->out.used)
    jsonwriter_output_buff_flush(&data->out);
}

void jsonwriter_delete(jsonwriter_handle data) {
  jsonwriter_flush(data);
  if(data->out.buff)
    free(data->out.buff);
  if(data->close_brackets)
    free(data->close_brackets);
  if(data->counts)
    free(data->counts);
  free(data);
}

static size_t jsonwriter_writeln(struct jsonwriter_data *data) {
  if(data->started)
    return jsonwriter_output_buff_write(&data->out, (const unsigned char *)"\n", 1);
  return 0;
}

static int jsonwriter_indent(struct jsonwriter_data *data, unsigned char closing) {
  if(data->just_wrote_key) {
    if(data->compact)
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)":", 1);
    else
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)": ", 2);
    data->just_wrote_key = 0;
    return 0;
  }

  if(data->depth) {
    if(!closing) { // add a value to the current list
      if(data->counts[data->depth - 1])
        jsonwriter_output_buff_write(&data->out, (const unsigned char *)",", 1);
      data->counts[data->depth - 1]++;
    }
  }

  if(!data->compact) {
    jsonwriter_writeln(data);
    for(int d = data->depth; d > 0; d--)
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)"  ", 2);
  }
  return 0;
}

static enum jsonwriter_status jsonwriter_end_aux(jsonwriter_handle data, unsigned char close_bracket) { // return 0 on success
  if(data->depth > 0) {
    if(close_bracket && data->close_brackets[data->depth-1] != close_bracket)
      return jsonwriter_status_invalid_end;

    data->depth--;

    if(data->depth < JSONWRITER_MAX_NESTING - 1) {
      jsonwriter_indent(data, 1);
      jsonwriter_output_buff_write(&data->out, data->close_brackets + data->depth, 1);
    }
    if(data->depth == 0)
      jsonwriter_writeln(data);
    return jsonwriter_status_ok;
  }
  return jsonwriter_status_invalid_end; // error: nothing to close
}

enum jsonwriter_status jsonwriter_end(jsonwriter_handle data) {
  return jsonwriter_end_aux(data, 0);
}

enum jsonwriter_status jsonwriter_end_array(jsonwriter_handle data) {
  return jsonwriter_end_aux(data, ']');
}

enum jsonwriter_status jsonwriter_end_object(jsonwriter_handle data) {
  return jsonwriter_end_aux(data, '}');
}

int jsonwriter_end_all(jsonwriter_handle data) {
  while(jsonwriter_end(data) == 0)
    ;
  return 0;
}

static int write_json_str(struct jsonwriter_output_buff *b,
                          const unsigned char *s, size_t len,
                          unsigned char no_quotes) {
  unsigned int replacelen;
  unsigned char replace[10];
  const unsigned char *end = s + len;
  const unsigned char *new_s;
  size_t written = 0;
  if(!no_quotes)
    jsonwriter_output_buff_write(b, (const unsigned char *)"\"", 1), written++;

  while(s < end) {
    replacelen = 0;
    unsigned int no_esc = json_esc1((const unsigned char *)s, len,
                                    &replacelen, replace, &new_s,
                                    len + sizeof(replace)
                                    );
    if(no_esc)
      jsonwriter_output_buff_write(b, s, no_esc), written += no_esc;
    if(replacelen)
      jsonwriter_output_buff_write(b, replace, replacelen), written += replacelen;
    if(new_s > s) {
      s = new_s;
      len = end - new_s;
    } else
      break;
  }
  if(!no_quotes)
    jsonwriter_output_buff_write(b, (const unsigned char *)"\"", 1), written += 1;

  return written;
}

static int jsonwriter_str1(jsonwriter_handle data, const unsigned char *s, size_t len) {
  write_json_str(&data->out, s, len, 0);
  return 0;
}

int jsonwriter_null(jsonwriter_handle data) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    jsonwriter_output_buff_write(&data->out, (const unsigned char *)"null", 4);
    return 0;
  }
  return 1;
}

int jsonwriter_bool(jsonwriter_handle data, unsigned char value) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    if(value)
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)"true", 4);
    else
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)"false", 5);
    return 0;
  }
  return 1;
}

int jsonwriter_cstr(jsonwriter_handle data, const char *s) {
  return jsonwriter_str(data, (const unsigned char *)s);
}

int jsonwriter_cstrn(jsonwriter_handle data, const char *s, size_t len) {
  return jsonwriter_strn(data, (const unsigned char *)s, len);
}

int jsonwriter_str(jsonwriter_handle data, const unsigned char *s) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    if(s)
      jsonwriter_str1(data, s, JSW_STRLEN(s));
    else
      jsonwriter_output_buff_write(&data->out, (const unsigned char *)"null", 4);
    return 0;
  }
  return 1;
}

int jsonwriter_strn(jsonwriter_handle data, const unsigned char *s, size_t len) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    jsonwriter_str1(data, s, len);
    return 0;
  }
  return 1;
}

static int jsonwriter_object_keyn(jsonwriter_handle data, const char *key, size_t len_or_zero) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    jsonwriter_str1(data, (const unsigned char *)key, len_or_zero == 0 ? JSW_STRLEN(key) : len_or_zero);
    data->just_wrote_key = 1;
    return 0;
  }
  return 1;
}

int jsonwriter_object_key(jsonwriter_handle data, const char *key) {
  return jsonwriter_object_keyn(data, key, strlen(key));
}

int jsonwriter_dblf(jsonwriter_handle data, long double d, const char *format_string,
                    unsigned char trim_trailing_zeros_after_dec) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    format_string = format_string ? format_string : "%Lf";
    int len = snprintf(data->tmp, sizeof(data->tmp), format_string, d);
    if(len && trim_trailing_zeros_after_dec && memchr(data->tmp, '.', len)) {
      while(len && data->tmp[len-1] == '0')
        len--;
      if(len && data->tmp[len-1] == '.')
        len--;
      if(!len) {
        *data->tmp = '0';
        len = 1;
      }
    }
    jsonwriter_output_buff_write(&data->out, (unsigned char *)data->tmp, len);
    return 0;
  }
  return 1;
}

int jsonwriter_dbl(jsonwriter_handle data, long double d) {
  return jsonwriter_dblf(data, d, NULL, 1);
}

int jsonwriter_int(jsonwriter_handle data, jsw_int64 i) {
  if(data->depth < JSONWRITER_MAX_NESTING) {
    jsonwriter_indent(data, 0);
    int len = snprintf(data->tmp, sizeof(data->tmp), JSW_INT64_PRINTF_FMT, i);
    jsonwriter_output_buff_write(&data->out, (unsigned char *)data->tmp, len);
    return 0;
  }
  return 1;
}

static int jsonwriter_go_deeper(struct jsonwriter_data *data, unsigned char open, unsigned char close) {
  if(data->depth < JSONWRITER_MAX_NESTING - 1) {
    jsonwriter_indent(data, 0);
    data->started = 1;
    jsonwriter_output_buff_write(&data->out, &open, 1);
    data->close_brackets[data->depth] = close;
    data->depth++;
    data->counts[data->depth-1] = 0;
    return 0;
  }
  return 1;
}

int jsonwriter_start_object(jsonwriter_handle data) {
  return jsonwriter_go_deeper((struct jsonwriter_data *)data, '{', '}');
}

int jsonwriter_start_array(jsonwriter_handle data) {
  return jsonwriter_go_deeper((struct jsonwriter_data *)data, '[', ']');
}

enum jsonwriter_status jsonwriter_set_variant_handler(
    jsonwriter_handle d,
    struct jsonwriter_variant (*to_jsw_variant)(void *),
    void (*cleanup)(void *, struct jsonwriter_variant *)
) {
  d->after_to_jsw_variant = cleanup;
  if(!(d->to_jsw_variant = to_jsw_variant))
    return jsonwriter_status_invalid_value;

  return jsonwriter_status_ok;
}

enum jsonwriter_status jsonwriter_variant(jsonwriter_handle d, void *data) {
  if(!d->to_jsw_variant)
    return jsonwriter_status_misconfiguration;
  struct jsonwriter_variant jv = d->to_jsw_variant(data);

  int rc = jsonwriter_status_unrecognized_variant_type;
  switch(jv.type) {
  case jsonwriter_datatype_null:
    rc = jsonwriter_null(d);
    break;
  case jsonwriter_datatype_string:
    rc = jsonwriter_str(d, jv.value.str);
    break;
  case jsonwriter_datatype_integer:
    rc = jsonwriter_int(d, jv.value.i);
    break;
  case jsonwriter_datatype_float:
    rc = jsonwriter_dbl(d, jv.value.dbl);
    break;
  case jsonwriter_datatype_bool:
    rc = jsonwriter_bool(d, jv.value.i ? 1 : 0);
    break;
  }
  if(d->after_to_jsw_variant)
    d->after_to_jsw_variant(data, &jv);
  return rc;
}
