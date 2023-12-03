/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/writer.h>
#include <zsv/utils/compiler.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

///
/* return 0 on eof, +1 on error, > 0 if valid utf8 first byte read */
static inline char UTF8_charLenC_noerr(int c) {
  char len;
  if(c == EOF) len = 0;
  else if(!(c & 128)) len = 1;
  else if((c & 224) == 192) len = 2; /* 110xxxxx */
  else if((c & 240) == 224) len = 3; /* 1110xxxx */
  else if((c & 248) == 240) len = 4; /* 11110xxx */
  else if((c & 252) == 248) len = 5; /* 111110xx */
  else if((c & 254) == 252) len = 6; /* 1111110x */
  else len = 1; // error, but we are going to ignore and just call it a 1-byte char
  return len;
}

static struct zsv_csv_writer_options zsv_csv_writer_default_opts = { 0 };
static char zsv_writer_default_opts_initd = 0;
void zsv_writer_set_default_opts(struct zsv_csv_writer_options opts) {
  zsv_writer_default_opts_initd = 1;
  zsv_csv_writer_default_opts = opts;
}

struct zsv_csv_writer_options zsv_writer_get_default_opts(void) {
  if(!zsv_writer_default_opts_initd) {
    zsv_writer_default_opts_initd = 1;
    zsv_csv_writer_default_opts.write = (size_t (*)(const void * restrict,  size_t,  size_t,  void * restrict))fwrite;
    zsv_csv_writer_default_opts.stream = stdout;
  }
  return zsv_csv_writer_default_opts;
}

// zsv_csv_quote() returns:
//   NULL if no quoting needed
//   buff if buff size was large enough to hold result
//   newly-allocated char * if buff not large enough, and was able to get from heap
// in last case, caller must free
unsigned char *zsv_csv_quote(const unsigned char *utf8_value,
                             size_t len,
                             unsigned char *buff, size_t buffsize) {
  char need = 0;
  unsigned quotes = 0;
  char clen;
  for(unsigned int i = 0; i < len; i+=clen) {
    if((clen = UTF8_charLenC_noerr(utf8_value[i])) == 1)
      switch(utf8_value[i]) {
      case ',':
      case '\n':
      case '\r':
        need = 1;
        break;
      case '"':
        need = 1;
        quotes++;
        break;
      }
  }
  if(!need)
    return NULL;

  unsigned char *target;
  unsigned mem_length = len + quotes + 3; // str + 2 quotes + terminating null
  if(mem_length < buffsize)
    target = buff;
  else
    target = malloc(mem_length * sizeof(*target));

  if(target) {
    *target = '"';
    if(!quotes)
      memcpy(target + 1, utf8_value, len);
    else {
      for(unsigned int i = 0, j = 0; i < len; i+=clen, j += clen) {
        if(utf8_value[i] == '"')
          target[++j] = '"';
        clen = UTF8_charLenC_noerr(utf8_value[i]);
        if(i + clen > len) // safety in case of invalid utf8 input
          clen = len - i;
        memcpy(target + 1 + j, utf8_value + i, clen);
      }
    }
    target[mem_length - 2] = '"';
    target[mem_length - 1] = '\0';
  }
  return target;
}

#define ZSV_OUTPUT_BUFF_SIZE 65536*4

struct zsv_output_buff {
  char *buff; // will be ZSV_OUTPUT_BUFF_SIZE. to do: option to modify buff size
  size_t (*write)(const void *restrict, size_t size, size_t nitems, void *restrict stream);
  void *stream;
  size_t used;
};

struct zsv_writer_data {
  size_t buffsize; // corresponds to buf
  unsigned char *buff; // option

  struct zsv_output_buff out;

  void (*table_init)(void *);
  void *table_init_ctx;

  const char *cell_prepend;

  unsigned char with_bom:1;
  unsigned char started:1;
  unsigned char _:6;
};

#include <unistd.h> // write


static inline void zsv_output_buff_flush(struct zsv_output_buff *b) {
  b->write(b->buff, b->used, 1, b->stream);
  b->used = 0;
}

static inline void zsv_output_buff_write(struct zsv_output_buff *b, const unsigned char *s, size_t n) {
  if(n) {
    if(n + b->used > ZSV_OUTPUT_BUFF_SIZE) {
      zsv_output_buff_flush(b);
      if(n > ZSV_OUTPUT_BUFF_SIZE) { // n too big, so write directly
        b->write(s, n, 1, b->stream);
        return;
      }
    }
    // n + used < buff size
    memcpy(b->buff + b->used, s, n);
    b->used += n;
  }
}

void zsv_writer_set_temp_buff(zsv_csv_writer w, unsigned char *buff,
                                size_t buffsize) {
  w->buff = buff;
  w->buffsize = buffsize;
}

zsv_csv_writer zsv_writer_new(struct zsv_csv_writer_options *opts) {
  struct zsv_writer_data *w = calloc(1, sizeof(*w));
  if(w) {
    if(!(w->out.buff = malloc(ZSV_OUTPUT_BUFF_SIZE))) {
      free(w); // out of memory!
      return NULL;
    }

    if(!opts) {
      w->out.write = (size_t (*)(const void * restrict,  size_t,  size_t,  void * restrict))fwrite;
      w->out.stream = stdout;
    } else {
      if(opts->write) {
        w->out.write = opts->write;
        w->out.stream = opts->stream;
      } else {
        w->out.write = (size_t (*)(const void * restrict,  size_t,  size_t,  void * restrict))fwrite;
        w->out.stream = opts->stream ? opts->stream : stdout;
      }

      w->with_bom = opts->with_bom;
      w->table_init = opts->table_init;
      w->table_init_ctx = opts->table_init_ctx;
    }
  }
  return w;
}

enum zsv_writer_status zsv_writer_flush(zsv_csv_writer w) {
  if(!w) return zsv_writer_status_missing_handle;

  zsv_output_buff_flush(&w->out);
  return zsv_writer_status_ok;
}

enum zsv_writer_status zsv_writer_delete(zsv_csv_writer w) {
  if(!w) return zsv_writer_status_missing_handle;

  zsv_output_buff_flush(&w->out);
  if(w->started)
    w->out.write("\n", 1, 1, w->out.stream);

  if(w->out.buff)
    free(w->out.buff);
  free(w);
  return zsv_writer_status_ok;
}

static inline
enum zsv_writer_status zsv_writer_cell_aux(zsv_csv_writer w,
                                           const unsigned char *s, size_t len,
                                           char check_if_needs_quoting) {
  if(len) {
    if(check_if_needs_quoting) {
      unsigned char *quoted_s = zsv_csv_quote(s, len, w->buff, w->buffsize);
      if(!quoted_s)
        zsv_output_buff_write(&w->out, s, len);
      else {
        zsv_output_buff_write(&w->out, quoted_s, strlen((char *)quoted_s));
        if(!(w->buff && quoted_s == w->buff))
          free(quoted_s);
      }
    } else
      zsv_output_buff_write(&w->out, s, len);
  }
  return zsv_writer_status_ok;
}

enum zsv_writer_status zsv_writer_cell(zsv_csv_writer w, char new_row,
                                       const unsigned char *s, size_t len,
                                       char check_if_needs_quoting) {
  if(!w) return zsv_writer_status_missing_handle;
  if(!w->started) {
    if(w->table_init)
      w->table_init(w->table_init_ctx);
    if(w->with_bom)
      zsv_output_buff_write(&w->out, (const unsigned char *)"\xef\xbb\xbf", 3);
    w->started = 1;
  } else if(new_row)
    zsv_output_buff_write(&w->out, (const unsigned char *)"\n", 1);
  else
    zsv_output_buff_write(&w->out, (const unsigned char *)",", 1);

  if(VERY_UNLIKELY(w->cell_prepend && *w->cell_prepend)) {
    char *tmp = NULL;
    asprintf(&tmp, "%s%.*s", w->cell_prepend, (int)len, s ? s : (const unsigned char *)"");
    if(!tmp)
      return zsv_writer_status_error; // zsv_writer_status_memory;
    s = (const unsigned char *)tmp;
    len = len + strlen(w->cell_prepend);
    enum zsv_writer_status stat = zsv_writer_cell_aux(w, s, len, 1);
    free(tmp);
    return stat;
  }
  return zsv_writer_cell_aux(w, s, len, check_if_needs_quoting);
}

void zsv_writer_cell_prepend(zsv_csv_writer w, const unsigned char *s) {
  w->cell_prepend = (const char *)s;
}

enum zsv_writer_status zsv_writer_cell_Lf(zsv_csv_writer w, char new_row, const char *fmt_spec,
                                              long double ldbl) {
  char s[128];
  char fmt[64];
  int n = snprintf(fmt, sizeof(fmt), "%%%sLf", fmt_spec);
  if(!(n > 0 && n < (int)sizeof(fmt)))
    fprintf(stderr, "Invalid format specifier, should be X for format %%XLf e.g. '.2'\n");
  else {
    n = snprintf(s, sizeof(s), fmt, ldbl);
    if(!(n > 0 && n < (int)sizeof(fmt)))
      fprintf(stderr, "Unable to format value with fmt %s: %Lf\n", fmt, ldbl);
    else
      return zsv_writer_cell(w, new_row, (unsigned char *)s, n, 0);
  }
  zsv_writer_cell(w, new_row, NULL, 0, 0);
  return zsv_writer_status_error;
}

enum zsv_writer_status zsv_writer_cell_blank(zsv_csv_writer w, char new_row) {
  return zsv_writer_cell(w, new_row, (const unsigned char *)"", 0, 0);
}

enum zsv_writer_status zsv_writer_cell_zu(zsv_csv_writer w, char new_row, size_t zu) {
  char s[64];
  int n = snprintf(s, sizeof(s), "%zu", zu);
  if(n < 1 || n >= (int)sizeof(s))
    n = 0; // unexpected overflow
  return zsv_writer_cell(w, new_row, (unsigned char *)s, n, 0);
}

enum zsv_writer_status zsv_writer_cell_s(zsv_csv_writer w, char new_row,
                                         const unsigned char *s,
                                         char check_if_needs_quoting) {
  return zsv_writer_cell(w, new_row, s, s ? strlen((const char *)s) : 0,
                           check_if_needs_quoting);
}

/*
 * returns: newly allocated value (caller must free) or NULL
 */
unsigned char *zsv_writer_str_to_csv(const unsigned char *s, size_t len) {
  if(len) {
    unsigned char *csv_s = zsv_csv_quote(s, len, NULL, 0);
    if(csv_s)
      return csv_s;
    csv_s = malloc(len + 1);
    memcpy(csv_s, s, len);
    csv_s[len] = '\0';
    return csv_s;
  }
  return NULL;
}
