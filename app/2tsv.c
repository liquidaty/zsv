/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum zsv_2tsv_status {
  zsv_2tsv_status_ok = 0,
  zsv_2tsv_status_out_of_memory
};

#define ZSV_2TSV_BUFF_SIZE 65536

struct static_buff {
  char *buff; // will be ZSV_2TSV_BUFF_SIZE
  size_t used;
  FILE *stream;
};

struct zsv_2tsv_data {
  zsv_parser parser;
  struct static_buff out;
  unsigned output_column_ix;
  unsigned char overflowed:1;
  unsigned char _:7;
};

static inline void zsv_2tsv_flush(struct static_buff *b) {
  fwrite(b->buff, b->used, 1, b->stream);
  b->used = 0;
}

static inline void zsv_2tsv_write(struct static_buff *b, const unsigned char *s, size_t n) {
  if(n) {
    if(n + b->used > ZSV_2TSV_BUFF_SIZE) {
      zsv_2tsv_flush(b);
      if(n > ZSV_2TSV_BUFF_SIZE) { // n too big, so write directly
        fwrite(s, n, 1, b->stream);
        return;
      }
    }
    // n + used < buff size
    memcpy(b->buff + b->used, s, n);
    b->used += n;
  }
}

// zsv_to_tsv(): convert, if necessary, to text suitable for tab-delimited output.
// - return NULL if nothing to convert
// - else, return allocated char * of converted tsv (caller must free), and update *lenp
// - on error, set *err
static unsigned char *zsv_to_tsv(const unsigned char *utf8, size_t len, enum zsv_2tsv_status *err) { // replace tab and newline with space
  size_t i;
  unsigned char charLen;
  const char replacement_char = ' '; // replacement
  char do_convert = 0;
  for(i = 0; i < len; i += charLen) {
    charLen = ZSV_UTF8_CHARLEN_NOERR((int)utf8[i]);
    if(charLen == 1)
      switch(utf8[i]) {
      case '\t':
      case '\n':
      case '\r':
        do_convert = 1;
        break;
      }
    if(do_convert)
      break;
  }

  if(!do_convert)
    return NULL;

  unsigned char *converted = malloc(len + 1);
  if(!converted)
    *err = zsv_2tsv_status_out_of_memory;
  else {
    memcpy(converted, utf8, len);
    for(; i < len; i += charLen) {
      charLen = ZSV_UTF8_CHARLEN_NOERR(converted[i]);
      if(charLen == 1)
        switch(converted[i]) {
        case '\t':
        case '\n':
        case '\r':
          converted[i] = replacement_char;
          break;
        }
    }
    converted[len] = '\0';
  }
  return converted;
}

static void zsv_2tsv_cell(void *ctx, unsigned char *utf8_value, size_t len) {
  struct zsv_2tsv_data *data = ctx;

  // output delimiter, if this is not the first column in the row
  if(data->output_column_ix++)
    zsv_2tsv_write(&data->out, (const unsigned char *)"\t", 1);

  // output cell contents (converted if necessary)
  if(len) {
    enum zsv_2tsv_status err = zsv_2tsv_status_ok;
    unsigned char *converted = zsv_to_tsv(utf8_value, len, &err);
    if(err != zsv_2tsv_status_ok)
      ; // handle out-of-memory error!
    else if(converted) {
      zsv_2tsv_write(&data->out, converted, len);
      free(converted);
    } else
      zsv_2tsv_write(&data->out, utf8_value, len);
  }
}

void zsv_2tsv_overflow(void *ctx, unsigned char *utf8_value, size_t len) {
  struct zsv_2tsv_data *data = ctx;
  if(len) {
    if(!data->overflowed) {
      fwrite("overflow! ", 1, strlen("overflow! "), stderr);
      fwrite(utf8_value, 1 ,len, stderr);
      fprintf(stderr, "(subsequent overflows will be suppressed)");
      data->overflowed = 1;
    }
  }
}

static void zsv_2tsv_row(void *ctx) {
  struct zsv_2tsv_data *data = ctx;
  data->output_column_ix = 0;
  zsv_2tsv_write(&data->out, (const unsigned char *) "\n", 1);
}

#ifndef MAIN
#define MAIN main
#endif

int MAIN(int argc, const char *argv[]) {
  FILE *f_in = NULL;
  struct zsv_2tsv_data data;
  memset(&data, 0, sizeof(data));
  int err = 0;
  for(int i = 1; !err && i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stdout, "Usage: zsv_2tsv [filename]\n");
      fprintf(stdout, "  Reads CSV input and converts to tsv");
      err = 1;
    } else if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = 1;
      else if(data.out.stream)
        fprintf(stderr, "Output file specified more than once\n"), err = 1;
      else if(!(data.out.stream = fopen(argv[i], "wb")))
        fprintf(stderr, "Unable to open for writing: %s\n", argv[i]), err = 1;
    } else {
      if(f_in)
        fprintf(stderr, "Input file specified more than once\n"), err = 1;
      else if(!(f_in = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = 1;
    }
  }

  if(err) {
    if(f_in)
      fclose(f_in);
    if(data.out.stream)
      fclose(data.out.stream);
    return 0;
  }

  if(!f_in) {
#ifdef NO_STDIN
    fprintf(stderr, "Please specify an input file\n");
    return 0;
#else
    f_in = stdin;
#endif
  }

  if(!data.out.stream)
    data.out.stream = stdout;

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));

  opts.cell = zsv_2tsv_cell;
  opts.row = zsv_2tsv_row;
  opts.ctx = &data;
  opts.overflow = zsv_2tsv_overflow;

  opts.stream = f_in;
  if((data.parser = zsv_new(&opts))) {
    char output[ZSV_2TSV_BUFF_SIZE];
    data.out.buff = output;

    zsv_handle_ctrl_c_signal();
    enum zsv_status status;
    while(!zsv_signal_interrupted
          && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
      ;
    zsv_finish(data.parser);
    zsv_delete(data.parser);
    zsv_2tsv_flush(&data.out);
  }

  if(f_in && f_in != stdin)
    fclose(f_in);
  if(data.out.stream)
    fclose(data.out.stream);

  return err;
}
