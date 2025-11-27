/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>

#define _GNU_SOURCE 1
#include <string.h>

#define ZSV_COMMAND check
#include "zsv_command.h"

struct zsv_check_data {
  FILE *in;
  const char *input_path;
  zsv_parser parser;
  size_t row_ix;
  size_t column_count;
  int err;
};

static void zsv_check_row(void *ctx) {
  struct zsv_check_data *data = ctx;
  size_t column_count = zsv_cell_count(data->parser);
  if (column_count != data->column_count) {
    printf("Row %zu column count (%zu) differs from header (%zu)\n", data->row_ix, column_count, data->column_count);
    data->err = 1;
  }
  data->row_ix++;
}

static void zsv_check_header(void *ctx) {
  struct zsv_check_data *data = ctx;
  data->column_count = zsv_cell_count(data->parser);
  data->row_ix++;
  zsv_set_row_handler(data->parser, zsv_check_row);
}

const char *zsv_check_usage_msg[] = {
  APPNAME ": check input for anomalies", "", "Usage: " APPNAME " <filename>", "", NULL,
};

static int zsv_check_usage(void) {
  for (size_t i = 0; zsv_check_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_check_usage_msg[i]);
  return 1;
}

static void zsv_check_cleanup(struct zsv_check_data *data) {
  if (data->in && data->in != stdin)
    fclose(data->in);
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  if (argc < 1 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_check_usage();
    return 0;
  }
  struct zsv_opts opts = *optsp;
  struct zsv_check_data data = {0};
  int err = 0;
  for (int arg_i = 1; !err && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
    /*
    if (!strcmp(arg, "-b"))
      writer_opts.with_bom = 1;
    else
    */
    if (!data.in) {
#ifndef NO_STDIN
      if (!strcmp(arg, "-"))
        data.in = stdin;
#endif
      if (!data.in) {
        if (!(data.in = fopen(arg, "rb"))) {
          err = 1;
          perror(arg);
        } else
          data.input_path = arg;
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

  if (!err && !data.in) {
#ifndef NO_STDIN
    data.in = stdin;
#else
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
#endif
  }

  if (err) {
    zsv_check_cleanup(&data);
    return 1;
  }

  opts.row_handler = zsv_check_header;
  opts.stream = data.in;
  opts.ctx = &data;
  if (zsv_new_with_properties(&opts, custom_prop_handler, data.input_path, &data.parser) != zsv_status_ok)
    err = 1;
  else {
    // process the input data
    zsv_handle_ctrl_c_signal();
    enum zsv_status status;
    while (!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
      ;
    zsv_finish(data.parser);
    zsv_delete(data.parser);
  }
  zsv_check_cleanup(&data);
  if (!err)
    err = data.err;
  return err;
}
