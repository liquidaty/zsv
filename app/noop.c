/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/signal.h>
#include <zsv/utils/arg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct data {
  int col_count;
  zsv_parser parser;
};

static void error(void *ctx, enum zsv_status status, const unsigned char *err_msg, size_t err_msg_len, unsigned char error_c, size_t cum_scanned_length) {
  (void)(ctx);
  (void)(status);
  (void)(err_msg);
  (void)(err_msg_len);
  (void)(error_c);
  (void)(cum_scanned_length);
}

static void cell(void *ctx, unsigned char *utf8_value, size_t len) {
  (void)(ctx);
  (void)(utf8_value);
  (void)(len);
}

static void overflow(void *ctx, unsigned char *utf8_value, size_t len) {
  (void)(ctx);
  (void)(utf8_value);
  (void)(len);
}

static void row(void *ctx) {
  (void)(ctx);
}

int main(int argc, const char *argv[]) {
  FILE *f = NULL;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct data data = { 0 };

  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stdout, "Usage: noop [filename]\n");
      fprintf(stdout, "  Reads CSV input and does nothing. For performance testing\n\n");
      return 0;
    } else if(!f) {
      if(!(f = fopen(argv[i], "rb"))) {
        fprintf(stderr, "Unable to open %s for writing\n", argv[i]);
        return 1;
      }
    } else {
      fprintf(stderr, "Input file specified more than once (second was %s)\n", argv[i]);
      if(f)
        fclose(f);
      return 1;
    }
  }

  if(!f)
    f = stdin;

  struct zsv_opts opts = zsv_get_default_opts();
  opts.cell = cell;
  opts.row = row;
  opts.ctx = &data;
  opts.overflow = overflow;
  opts.error = error;

  opts.stream = f;
  if((data.parser = zsv_new(&opts))) {
    zsv_handle_ctrl_c_signal();
    enum zsv_status status;
    while(!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
      ;
    zsv_finish(data.parser);
    zsv_delete(data.parser);
  }
  fclose(f);
  return 0;
}
