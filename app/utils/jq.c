#include <string.h>
#include <stdlib.h>
#include <zsv/utils/string.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/jq.h>
#include <jq.h>
#include <jv.h>

// all jv_ functions defined here consume their jv value
// consumes:
//  jv_string_length_bytes
//  jv_dumpf
//  jv_dump_string
//
// does not consume:
//  jv_number_value
//  jv_string_value
//  jv_get_kind

static int jv_print_scalar_str(jv value, char inside_string, FILE *f, char as_csv) {
  f = f ? f : stdout;
  switch(jv_get_kind(value)) {
  case JV_KIND_STRING:
    {
      size_t len = jv_string_length_bytes(jv_copy(value));
      const char *s = jv_string_value(value);
      if(!as_csv)
        fprintf(f, "%.*s", (int)len, s);
      else {
        unsigned char *csv = zsv_csv_quote((unsigned char *)s, len, NULL, 0);
        if(!csv)
          fprintf(f, "%.*s", (int)len, s);
        else {
          if(inside_string)
            fprintf(f, "%s%.*s", inside_string > 1 ? ";" : "", (int)(strlen((char *)csv) - 2), csv + 1);
          else
            fprintf(f, "%s", csv);
          free(csv);
        }
      }
      jv_free(value);
      return 1;
    }
    break;
  default:
    break;
  }
  jv_free(value);
  return 0;
}

static int jv_print_scalar(jv value, char inside_string, FILE *f, char as_csv) {
  f = f ? f : stdout;
  switch(jv_get_kind(value)) {
  case JV_KIND_INVALID:
    fprintf(f, "<invalid json>");
    jv_free(value);
    return 1;
  case JV_KIND_NULL:
    fprintf(f, "null");
    jv_free(value);
    return 1;
  case JV_KIND_TRUE:
    fprintf(f, "true");
    jv_free(value);
    return 1;
  case JV_KIND_FALSE:
    fprintf(f, "false");
    jv_free(value);
    return 1;
  case JV_KIND_NUMBER:
    {
      char s[64];
      int n = snprintf(s, sizeof(s), "%lf", jv_number_value(value));
      if(n > 0 && (size_t) n < sizeof(s))
        fprintf(f, "%.*s", (int)zsv_strip_trailing_zeros(s, n), s);
    }
    jv_free(value);
    return 1;
  default:
    return jv_print_scalar_str(value, inside_string, f, as_csv);
  }
}

static void jv_to_csv_aux(jv value, FILE *f, int inside_string) {
  f = f ? f : stdout;
  if(!jv_print_scalar(jv_copy(value), inside_string, f, 1)) {
    switch(jv_get_kind(value)) {
    case JV_KIND_ARRAY:
      // flatten
      if(!inside_string)
        fprintf(f, "\"");
      jv_array_foreach(value, i, item) {
        if(i)
          fprintf(f, ";");
        jv_to_csv_aux(item, f, 1);
      }
      if(!inside_string)
        fprintf(f, "\"");
      break;
    case JV_KIND_OBJECT:
      // flatten
      if(!inside_string)
        fprintf(f, "\"");
      jv_object_foreach(value, key, item) {
        jv_print_scalar(key, 1, f, 1);
        fprintf(f, ":");
        jv_to_csv_aux(item, f, 1);
        fprintf(f, ";");
      }
      if(!inside_string)
        fprintf(f, "\"");
      break;
    default:
      break;
    }
  }
  jv_free(value);
}

size_t zsv_jq_fwrite1(void *restrict FILE_ptr, const void *restrict buff, size_t len) {
  return fwrite(buff, len, 1, FILE_ptr);
}

void jv_to_json_func(jv value, void *ctx) {
  struct jv_to_json_ctx *data = ctx;
  if(data->write1 == zsv_jq_fwrite1)
    jv_dumpf(value, data->ctx, data->flags);
  else {
    // jv_dump_string is memory-inefficient
    // would be better to create custom dump function that, instead of writing to string buffer,
    // could directly invoke func()
    jv jv_s = jv_dump_string(value, data->flags);
    const char* p = jv_string_value(jv_s);
    size_t len = jv_string_length_bytes(jv_copy(jv_s));
    if(len)
      data->write1(data->ctx, p, len);
    jv_free(jv_s);
  }
}

void jv_to_csv_multi(jv value, void *jv_to_csv_multi_ctx) {
  struct jv_to_csv_multi_ctx *ctx = jv_to_csv_multi_ctx;
  FILE *f = ctx->get_file(ctx->ctx);
  jv_to_csv(value, f);
}

void jv_to_csv(jv value, void *file) {
  FILE *f = file;
  f = f ? f : stdout;
  if(jv_print_scalar(jv_copy(value), 0, f, 1))
    fprintf(f, "\n");
  else {
    switch(jv_get_kind(value)) {
    case JV_KIND_ARRAY:
      jv_array_foreach(value, i, item) {
        if(i)
          fprintf(f, ",");
        jv_to_csv_aux(item, f, 0);
      }
      fprintf(f, "\n");
      break;
    case JV_KIND_OBJECT:
      jv_object_foreach(value, key, item) {
        jv_print_scalar(key, 0, f, 1);
        fprintf(f, ",");
        jv_to_csv_aux(item, f, 0);
        fprintf(f, "\n");
      }
      fprintf(f, "\n");
      break;
    default:
      break;
    }
  }
  jv_free(value);
}

static void jv_to_txt_aux(jv value, FILE *f) {
  f = f ? f : stdout;
  if(!jv_print_scalar(jv_copy(value), 0, f, 0)) {
    switch(jv_get_kind(value)) {
    case JV_KIND_ARRAY:
      // flatten
      fprintf(f, "[");
      jv_array_foreach(value, i, item) {
        if(i)
          fprintf(f, ";");
        jv_to_txt_aux(item, f);
      }
      fprintf(f, "]");
      break;
    case JV_KIND_OBJECT:
      // flatten
      fprintf(f, "{");
      jv_object_foreach(value, key, item) {
        jv_print_scalar(key, 1, f, 0);
        fprintf(f, ":");
        jv_to_txt_aux(item, f);
        fprintf(f, ";");
      }
      fprintf(f, "}");
      break;
    default:
      break;
    }
  }
  jv_free(value);
}

void jv_to_txt(jv value, void *file) {
  FILE *f = file;
  f = f ? f : stdout;
  if(jv_print_scalar(jv_copy(value), 0, f, 0))
    fprintf(f, "\n");
  else {
    switch(jv_get_kind(value)) {
    case JV_KIND_ARRAY:
      jv_array_foreach(value, i, item) {
        if(i)
          fprintf(f, ",");
        jv_to_txt_aux(item, f);
      }
      fprintf(f, "\n");
      break;
    case JV_KIND_OBJECT:
      jv_object_foreach(value, key, item) {
        jv_print_scalar(key, 0, f, 0);
        fprintf(f, ",");
        jv_to_txt_aux(item, f);
        fprintf(f, "\n");
      }
      fprintf(f, "\n");
      break;
    default:
      break;
    }
  }
  jv_free(value);
}

void jv_to_lqjq(jv value, void *h) {
  zsv_jq_handle lqjq = h;

  // jv_dump_string is memory-inefficient
  // would be better to create custom dump function that, instead of writing to string buffer,
  // could directly invoke func()
  jv jv_s = jv_dump_string(value, 0);
  size_t len = jv_string_length_bytes(jv_copy(jv_s));
  const char* p = jv_string_value(jv_s);
  if(len)
    zsv_jq_parse(lqjq, p, len);
  jv_free(jv_s);
}

struct zsv_jq_data {
  void *jq;
  struct jv_parser *parser;
  void (*func)(jv, void *);
  void *ctx;

  FILE *trace;
  enum zsv_jq_status status;
  unsigned char non_null:1;
  unsigned char _:7;
};

static
zsv_jq_handle zsv_jq_new_aux(const unsigned char *filter,
                           void (*func)(jv, void *), void *ctx,
                           enum zsv_jq_status *statusp,
                           int init_flags) {
  enum zsv_jq_status status = zsv_jq_status_ok;
  struct zsv_jq_data *d = calloc(1, sizeof(*d));
  if(!d || !(d->jq = jq_init()) || !(d->parser = jv_parser_new(init_flags)))
    status = zsv_jq_status_memory;
  else if(!jq_compile(d->jq, (const char *)filter))
    status = d->status = zsv_jq_status_compile;
  if(status == zsv_jq_status_ok) {
    d->func = func;
    d->ctx = ctx;
  } else {
    zsv_jq_delete(d);
    d = NULL;
  }
  if(statusp)
    *statusp = status;
  return d;
}

zsv_jq_handle zsv_jq_new(const unsigned char *filter,
                       void (*func)(jv, void *), void *ctx,
                       enum zsv_jq_status *statusp) {
  return zsv_jq_new_aux(filter, func, ctx, statusp, 0);
}

zsv_jq_handle zsv_jq_new_stream(const unsigned char *filter,
                              void (*func)(jv, void *), void *ctx,
                              enum zsv_jq_status *statusp) {
  return zsv_jq_new_aux(filter, func, ctx, statusp, JV_PARSE_STREAMING);
}


void zsv_jq_delete(zsv_jq_handle h) {
  if(h) {
    if(h->parser)
      jv_parser_free(h->parser);
    if(h->jq)
      jq_teardown((jq_state **)&h->jq);
    free(h);
  }
}

static int zsv_jq_process(jq_state *jq, jv value,
                  void (*func)(jv, void *), void *ctx);

size_t zsv_jq_write(const char *s, size_t n, size_t m, zsv_jq_handle h) {
  zsv_jq_parse(h, s, n * m);
  return n * m;
}

enum zsv_jq_status zsv_jq_parse_file(zsv_jq_handle h, FILE *f) {
  char buff[4096];
  for(size_t bytes_read = fread(buff, 1, sizeof(buff), f);
      bytes_read && h->status == zsv_jq_status_ok;
      bytes_read = fread(buff, 1, sizeof(buff), f)) {
    zsv_jq_parse(h, buff, bytes_read);
    if(feof(f))
      break;
  }
  return h->status;
}

enum zsv_jq_status zsv_jq_parse(zsv_jq_handle restrict h, const void * restrict s, size_t len) {
  if(h->status != zsv_jq_status_ok)
    return h->status;

  jv_parser_set_buf(h->parser, (const char *)s, len, 1);
  if(h->trace)
    fwrite(s, len, 1, h->trace);

  jv value;
  while(jv_is_valid(value = jv_parser_next(h->parser))) {
    if(!h->non_null && jv_get_kind(value) != JV_KIND_NULL)
      h->non_null = 1;
    zsv_jq_process(h->jq, value, h->func, h->ctx);
  }

  jv msg = jv_invalid_get_msg(value);
  if(jv_get_kind(msg) == JV_KIND_STRING) {
    fprintf(stderr, "jq: parse error: %s\n", jv_string_value(msg));
    h->status = zsv_jq_status_error;
  } else
    jv_free(msg);
  jv_free(value);

  return h->status;
}

void zsv_jq_trace(zsv_jq_handle h, FILE *trace) {
  if(h)
    h->trace = trace;
}

enum zsv_jq_status zsv_jq_finish(zsv_jq_handle h) {
  jv value;
  jv_parser_set_buf(h->parser, "", 0, 0);
  while(jv_is_valid(value = jv_parser_next(h->parser)))
    zsv_jq_process(h->jq, value, h->func, h->ctx);
  return zsv_jq_status_ok;
}

static int zsv_jq_process(jq_state *jq,
                         jv value, // will be consumed
                         void (*func)(jv, void *),
                         void *ctx) {
  int ret = 14; // No valid results && -e -> exit(4)
  jq_start(jq, value, 0); // consumes value

  jv result;
  while(jv_is_valid(result = jq_next(jq))) {
    ret = 0;
    func(result, ctx);
  }
  jv_free(result);

  return ret;
}

void jv_to_bool(jv value, void *char_result) {
  char *c = char_result;
  switch(jv_get_kind(value)) {
  case JV_KIND_TRUE:
    *c = 1;
    break;
  case JV_KIND_FALSE:
    *c = 0;
    break;
  case JV_KIND_NUMBER:
    *c = (jv_number_value(value) > 0);
    break;
  default:
    break;
  }
  jv_free(value);
}
