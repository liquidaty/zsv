/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <errno.h>

#define ZSV_COMMAND check
#include "zsv_command.h"

#define _GNU_SOURCE 1
#include <string.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/err.h>

#ifdef USE_SIMDUTF
#include "check/simdutf_wrapper.h"
#define USAGE_APPNAME APPNAME " (simdutf8)"
#else
#include "check/utf8.c"
#define USAGE_APPNAME APPNAME
#endif

struct zsv_check_data {
  FILE *in;
  FILE *out;
  const char *input_path;
  zsv_parser parser;
  size_t row_ix;
  size_t column_count;
  int err;
  unsigned char display_row : 1;
  unsigned char check_utf8 : 1;
  unsigned char _ : 6;
};

#ifdef USE_SIMDUTF
#define UTF8VALIDATOR simdutf_is_valid_utf8
#else
#define UTF8VALIDATOR utf8_is_valid
#endif

static void zsv_check_row(void *ctx) {
  struct zsv_check_data *data = ctx;
  size_t column_count = zsv_cell_count(data->parser);
  unsigned const char *row_start = NULL;
  size_t row_len;
  if (column_count != data->column_count) {
    fprintf(data->out, "Row %zu column count (%zu) differs from header (%zu)", data->row_ix, column_count,
            data->column_count);
    data->err = 1;
    if (data->display_row && column_count > 0) {
      row_start = zsv_get_cell(data->parser, 0).str;
      struct zsv_cell last_cell = zsv_get_cell(data->parser, column_count - 1);
      row_len = (last_cell.str + last_cell.len - row_start);
      fprintf(data->out, ": %.*s", (int)row_len, row_start);
    }
    fprintf(data->out, "\n");
  }
  if (data->check_utf8) {
    if (!row_start) {
      row_start = zsv_get_cell(data->parser, 0).str;
      struct zsv_cell last_cell = zsv_get_cell(data->parser, column_count - 1);
      row_len = (last_cell.str + last_cell.len - row_start);
    }
    if (row_len > 0 && !UTF8VALIDATOR(row_start, row_len)) {
      data->err = 1;
      fprintf(data->out, "Row %zu invalid utf8", data->row_ix);
      if (data->display_row)
        fprintf(data->out, ": %.*s", (int)row_len, row_start);
      fprintf(data->out, "\n");
    }
  }
  data->row_ix++;
}

static void zsv_check_header(void *ctx) {
  struct zsv_check_data *data = ctx;
  data->column_count = zsv_cell_count(data->parser);
  data->row_ix++;
  zsv_set_row_handler(data->parser, zsv_check_row);
}

static int zsv_check_usage(void) {
  const char *zsv_check_usage_msg[] = {
    USAGE_APPNAME ": check input for anomalies",
    "",
    "Usage: " APPNAME " <filename>",
    "",
    "Options:",
    "  -o,--output <path> : output to specified file path",
    "  --display-row      : display the row contents with any reported issue",
    //    "  --utf8             : check for invalid utf8",
    // "  --all              : run all checks",
    "",
    "If no check options are provided, all checks are run",
    NULL,
  };
  for (size_t i = 0; zsv_check_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_check_usage_msg[i]);
  return 1;
}

static void zsv_check_cleanup(struct zsv_check_data *data) {
  if (data->in && data->in != stdin)
    fclose(data->in);
  if (data->out && data->out != stdout)
    fclose(data->out);
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
    if (!strcmp(arg, "--display-row"))
      data.display_row = 1;
    // else if (!strcmp(arg, "--utf8"))
    // data.check_utf8 = 1;
    else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
      if (data.out)
        err = zsv_printerr(1, "Output specified more than once");
      else {
        const char *fn = zsv_next_arg(++arg_i, argc, argv, &err);
        if (!(fn && *fn))
          err = zsv_printerr(1, "%s requires a filename value", arg);
        else if (!(data.out = fopen(fn, "wb"))) {
          err = errno;
          perror(fn);
        }
      }
    } else if (!data.in) {
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

  data.check_utf8 = 1;
  if (!data.out)
    data.out = stdout;
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
