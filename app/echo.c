/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct data {
  zsv_parser parser;
  zsv_csv_writer csv_writer;
};

static void row(void *ctx) {
  struct data *data = ctx;
  unsigned j = zsv_column_count(data->parser);
  for(unsigned i = 0; i < j; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    zsv_writer_cell(data->csv_writer, i == 0, c.str, c.len, c.quoted);
  }
}

#ifndef MAIN
#define MAIN main
#endif

int MAIN(int argc, const char *argv[]) {
  FILE *f = NULL;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct data data;
  memset(&data, 0, sizeof(data));
  char tab = 0;

  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stdout, "Usage: csv_echo [-b, --with-bom] [filename]\n");
      fprintf(stdout, "  Reads CSV input and prints CSV output with BOM prefix\n\n");
      fprintf(stdout, "Options:\n");
      fprintf(stdout, "    -b, --with-bom: print byte-order mark\n");
      fprintf(stdout, "    -T: input is tab-delimited, instead of comma-delimited\n");
      return 0;
    } else if(!strcmp(argv[i], "-b") || !strcmp(argv[i], "--with-bom"))
      writer_opts.with_bom = 1;
    else if(!strcmp(argv[i], "-T"))
      tab = 1;
    else if(!f) {
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

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));

  opts.row = row;
  opts.ctx = &data;
  if(tab)
    opts.delimiter = '\t';

  int err = 0;
  if((data.csv_writer = zsv_writer_new(&writer_opts))) {
    unsigned char writer_buff[64];
    zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));
    opts.stream = f;
    if((data.parser = zsv_new(&opts))) {
      zsv_handle_ctrl_c_signal();
      enum zsv_status status;
      while(!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;
      zsv_finish(data.parser);
      zsv_delete(data.parser);
    }
    zsv_writer_delete(data.csv_writer);
  }
  fclose(f);
  return err;
}            




