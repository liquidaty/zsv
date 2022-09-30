/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ZSV_COMMAND count
#include "zsv_command.h"

#include <zsv/utils/writer.h>

struct data {
  zsv_parser parser;
  zsv_csv_writer csv_writer;
};

static void row(void *ctx) {
  struct data *data = ctx;
  unsigned j = zsv_cell_count(data->parser);
  for(unsigned i = 0; i < j; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    zsv_writer_cell(data->csv_writer, i == 0, c.str, c.len, c.quoted);
  }
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, const char *opts_used) {
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct data data = { 0 };
  const char *input_path = NULL;

  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stdout, "Usage: csv_echo [-b, --with-bom] [filename]\n");
      fprintf(stdout, "  Reads CSV input and prints CSV output with BOM prefix\n\n");
      fprintf(stdout, "Options:\n");
      fprintf(stdout, "    -b, --with-bom: print byte-order mark\n");
      return 0;
    } else if(!strcmp(argv[i], "-b") || !strcmp(argv[i], "--with-bom"))
      writer_opts.with_bom = 1;
    else if(!opts->stream) {
      if(!(opts->stream = fopen(argv[i], "rb"))) {
        fprintf(stderr, "Unable to open %s for writing\n", argv[i]);
        return 1;
      } else
        input_path = argv[i];
    } else {
      fprintf(stderr, "Input file specified more than once (second was %s)\n", argv[i]);
      if(opts->stream)
        fclose(opts->stream);
      return 1;
    }
  }

  if(!opts->stream)
    opts->stream = stdin;

  opts->row_handler = row;
  opts->ctx = &data;

  if((data.csv_writer = zsv_writer_new(&writer_opts))) {
    unsigned char writer_buff[64];
    zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));
    if(zsv_new_with_properties(opts, input_path, opts_used, &data.parser) == zsv_status_ok) {
      zsv_handle_ctrl_c_signal();
      enum zsv_status status;
      while(!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;
      zsv_finish(data.parser);
      zsv_delete(data.parser);
    }
    zsv_writer_delete(data.csv_writer);
  }
  if(opts->stream != stdin)
    fclose(opts->stream);
  return 0;
}
