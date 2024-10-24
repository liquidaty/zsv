/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

#define _GNU_SOURCE 1
#include <string.h>
#include <ctype.h>

#define ZSV_COMMAND echo
#include "zsv_command.h"

#include <zsv/utils/compiler.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/string.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/overwrite.h>

struct zsv_echo_data {
  FILE *in;
  const char *input_path;
  zsv_csv_writer csv_writer;
  zsv_parser parser;
  size_t row_ix;

  unsigned char *skip_until_prefix;
  size_t skip_until_prefix_len;

  char *tmp_fn;
  unsigned max_nonempty_cols;
  unsigned char trim_white : 1;
  unsigned char trim_columns : 1;
  unsigned char contiguous : 1;
  unsigned char _ : 5;
};

static void zsv_echo_get_max_nonempty_cols(void *hook) {
  struct zsv_echo_data *data = hook;
  unsigned row_nonempty_col_count = 0;
  for (size_t i = 0, j = zsv_cell_count(data->parser); i < j; i++) {
    struct zsv_cell cell = zsv_get_cell(data->parser, i);
    if (UNLIKELY(data->trim_white))
      cell.str = (unsigned char *)zsv_strtrim(cell.str, &cell.len);
    if (cell.len)
      row_nonempty_col_count = i + 1;
  }
  if (data->max_nonempty_cols < row_nonempty_col_count)
    data->max_nonempty_cols = row_nonempty_col_count;
}

static void zsv_echo_row(void *hook) {
  struct zsv_echo_data *data = hook;
  size_t j = zsv_cell_count(data->parser);
  if (UNLIKELY(data->trim_columns && j > data->max_nonempty_cols))
    j = data->max_nonempty_cols;

  if (VERY_UNLIKELY(data->row_ix == 0)) { // header
    for (size_t i = 0; i < j; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      if (UNLIKELY(data->trim_white))
        cell.str = (unsigned char *)zsv_strtrim(cell.str, &cell.len);
      zsv_writer_cell(data->csv_writer, i == 0, cell.str, cell.len, cell.quoted);
    }
  } else if (VERY_UNLIKELY(data->contiguous && zsv_row_is_blank(data->parser))) {
    zsv_abort(data->parser);
  } else {
    for (size_t i = 0; i < j; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      if (UNLIKELY(data->trim_white))
        cell.str = (unsigned char *)zsv_strtrim(cell.str, &cell.len);
      zsv_writer_cell(data->csv_writer, i == 0, cell.str, cell.len, cell.quoted);
    }
  }
  data->row_ix++;
}

static void zsv_echo_row_skip_until(void *hook) {
  struct zsv_echo_data *data = hook;
  struct zsv_cell cell = zsv_get_cell(data->parser, 0);
  if (cell.len && cell.str && cell.len >= data->skip_until_prefix_len &&
      (!data->skip_until_prefix_len ||
       !zsv_strincmp(cell.str, data->skip_until_prefix_len, data->skip_until_prefix, data->skip_until_prefix_len))) {
    zsv_set_row_handler(data->parser, zsv_echo_row);
    zsv_echo_row(hook);
  }
}

const char *zsv_echo_usage_msg[] = {
  APPNAME ": write tabular input to stdout with optional cell overwrites",
  "",
  "Usage: " APPNAME " [options] <filename>",
  "",
  "Options:",
  "  -b                   : output with BOM",
  "  -o <filename>        : filename to save output to",
  "  --trim               : trim whitespace",
  "  --trim-columns       : trim blank columns",
  "  --contiguous         : stop output upon scanning an entire row of blank values",
  "  --skip-until <value> : skip rows until the row where first column starts with the given value",
  "  --overwrite <source> : overwrite cells using given source",
  "",
  "For --overwrite, the <source> may be:",
  "- sqlite3://<filename>[?sql=<query>]",
  "  e.g. sqlite3://overwrites.db?sql=select row, column, value from overwrites order by row, column",
  "",
  "- /path/to/file.csv",
  "  path to CSV file with columns row,col,val (in that order) and rows pre-sorted by row and column",
  NULL,
};

static int zsv_echo_usage() {
  for (size_t i = 0; zsv_echo_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_echo_usage_msg[i]);
  return 1;
}

static void zsv_echo_cleanup(struct zsv_echo_data *data) {
  zsv_writer_delete(data->csv_writer);
  free(data->skip_until_prefix);
  if (data->in && data->in != stdin)
    fclose(data->in);

  if (data->tmp_fn) {
    remove(data->tmp_fn);
    free(data->tmp_fn);
  }
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (argc < 1 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_echo_usage();
    return 0;
  }
  struct zsv_opts opts = *optsp;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct zsv_echo_data data = {0};
  struct zsv_overwrite_opts overwrite_opts = {0};

  int err = 0;

  for (int arg_i = 1; !err && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
    if (!strcmp(arg, "-b"))
      writer_opts.with_bom = 1;
    else if (!strcmp(arg, "-o")) {
      const char *output_filename = zsv_next_arg(++arg_i, argc, argv, &err);
      if (output_filename && !err) {
        if (!(writer_opts.stream = fopen(output_filename, "wb")))
          perror(output_filename);
      }
    } else if (!strcmp(arg, "--contiguous"))
      data.contiguous = 1;
    else if (!strcmp(arg, "--trim-columns"))
      data.trim_columns = 1;
    else if (!strcmp(arg, "--trim"))
      data.trim_white = 1;
    else if (!strcmp(arg, "--skip-until")) {
      if (++arg_i >= argc) {
        fprintf(stderr, "Option %s requires a value\n", arg);
        err = 1;
      } else if (!argv[arg_i][0]) {
        fprintf(stderr, "--skip-until requires a non-empty value\n");
        err = 1;
      } else {
        free(data.skip_until_prefix);
        data.skip_until_prefix = (unsigned char *)strdup(argv[arg_i]);
        data.skip_until_prefix_len = data.skip_until_prefix ? strlen((char *)data.skip_until_prefix) : 0;
      }
    } else if (!strcmp(arg, "--overwrite"))
      overwrite_opts.src = zsv_next_arg(++arg_i, argc, argv, &err);
    else if (!data.in) {
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
    fprintf(stderr, "No input\n");
    err = 1;
#endif
  }

  if (err) {
    zsv_echo_cleanup(&data);
    return 1;
  }

  unsigned char buff[4096];
  if (data.skip_until_prefix)
    opts.row_handler = zsv_echo_row_skip_until;
  else {
    if (data.trim_columns) {
      // first, save the file if it is stdin
      if (data.in == stdin) {
        if (!(data.tmp_fn = zsv_get_temp_filename("zsv_echo_XXXXXXXX"))) {
          zsv_echo_cleanup(&data);
          return 1;
        }

        FILE *f = fopen(data.tmp_fn, "wb");
        if (!f) {
          perror(data.tmp_fn);
          zsv_echo_cleanup(&data);
          return 1;
        } else {
          size_t bytes_read;
          while ((bytes_read = fread(buff, 1, sizeof(buff), data.in)) > 0)
            fwrite(buff, 1, bytes_read, f);
          fclose(f);
          if (!(data.in = fopen(data.tmp_fn, "rb"))) {
            perror(data.tmp_fn);
            zsv_echo_cleanup(&data);
            return 1;
          }
        }
      }
      // next, determine the max number of columns from the left that contains data
      struct zsv_opts tmp_opts = opts;
      tmp_opts.row_handler = zsv_echo_get_max_nonempty_cols;
      tmp_opts.stream = data.in;
      tmp_opts.ctx = &data;
      if (zsv_new_with_properties(&tmp_opts, custom_prop_handler, data.input_path, opts_used, &data.parser) !=
          zsv_status_ok) {
        zsv_echo_cleanup(&data);
        return 1;
      } else {
        // find the max nonempty col count
        enum zsv_status status;
        while (!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
          ;
        zsv_finish(data.parser);
        zsv_delete(data.parser);
        data.parser = NULL;

        // re-open the input again
        data.in = fopen(data.tmp_fn ? data.tmp_fn : data.input_path, "rb");
      }
    }
    opts.row_handler = zsv_echo_row;
  }
  opts.stream = data.in;
  opts.ctx = &data;

  data.csv_writer = zsv_writer_new(&writer_opts);
  if (overwrite_opts.src) {
    if(!(opts.overwrite.ctx = zsv_overwrite_context_new(&overwrite_opts))) {
      fprintf(stderr, "Out of memory!\n");
      err = 1;
    } else {
      opts.overwrite.open = zsv_overwrite_open;
      opts.overwrite.next = zsv_overwrite_next;
      opts.overwrite.close = zsv_overwrite_context_delete;
    }
  }

  if (data.csv_writer && !err) {
    if(zsv_new_with_properties(&opts, custom_prop_handler, data.input_path, opts_used, &data.parser) != zsv_status_ok)
      err = 1;
    else {
      // create a local csv writer buff for faster performance
      // unsigned char writer_buff[64];
      zsv_writer_set_temp_buff(data.csv_writer, buff, sizeof(buff));

      // process the input data
      zsv_handle_ctrl_c_signal();
      enum zsv_status status;
      while (!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;

      zsv_finish(data.parser);
      zsv_delete(data.parser);
    }
  }
  zsv_echo_cleanup(&data);
  return err;
}
