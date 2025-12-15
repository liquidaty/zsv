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
#include <zsv/utils/os.h>
#ifdef ZSV_EXTRAS
#include <zsv/utils/overwrite.h>
#endif

struct zsv_echo_data {
  FILE *in;
  const char *input_path;
  zsv_csv_writer csv_writer;
  zsv_parser parser;
  size_t row_ix;
  size_t start_row, end_row;

  unsigned char *skip_until_prefix;
  size_t skip_until_prefix_len;

  char *tmp_fn;
  unsigned max_nonempty_cols;
  unsigned char trim_white : 1;
  unsigned char trim_columns : 1;
  unsigned char contiguous : 1;
  unsigned char _ : 5;
};

static void zsv_echo_get_max_nonempty_cols(void *ctx) {
  struct zsv_echo_data *data = ctx;
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

static void zsv_echo_row(void *ctx) {
  struct zsv_echo_data *data = ctx;
  if (VERY_UNLIKELY((data->end_row > 0 && data->end_row <= data->row_ix))) {
    zsv_abort(data->parser);
    return;
  }
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

static void zsv_echo_row_skip_until(void *ctx) {
  struct zsv_echo_data *data = ctx;
  struct zsv_cell cell = zsv_get_cell(data->parser, 0);
  if (cell.len && cell.str && cell.len >= data->skip_until_prefix_len &&
      (!data->skip_until_prefix_len ||
       !zsv_strincmp(cell.str, data->skip_until_prefix_len, data->skip_until_prefix, data->skip_until_prefix_len))) {
    zsv_set_row_handler(data->parser, zsv_echo_row);
    zsv_echo_row(ctx);
  }
}

static void zsv_echo_row_start_at(void *ctx) {
  struct zsv_echo_data *data = ctx;
  if (data->row_ix >= data->start_row - 1) {
    void (*row_handler)(void *) = data->skip_until_prefix ? zsv_echo_row_skip_until : zsv_echo_row;
    zsv_set_row_handler(data->parser, row_handler);
    row_handler(ctx);
  } else
    data->row_ix++;
}

const char *zsv_echo_usage_msg[] = {
#ifdef ZSV_EXTRAS
  APPNAME ": write tabular input to stdout with optional cell overwrites",
#else
  APPNAME ": write tabular input to stdout",
#endif
  "",
  "Usage: " APPNAME " [options] <filename>",
  "",
  "Options:",
  "  -b                     : output with BOM",
  "  -o <filename>          : filename to save output to",
  "  --trim                 : trim whitespace",
  "  --trim-columns         : trim blank columns",
  "  --contiguous           : stop output upon scanning an entire row of blank values",
  "  --start-row    <N>     : only output from row N (starting at 1)",
  "  --end-row      <N>     : only output up to row N (starting at 1)",
  "  --between-rows <N> <M> : equivalent to --start-row N --end-row M",
  "  --skip-until <value>   : skip rows until the row where first column starts with the given value",
#ifdef ZSV_EXTRAS
  "  --overwrite <source>   : overwrite cells using given source",
  "",
  "For --overwrite, the <source> may be:",
  "- sqlite3://<filename>[?sql=<query>]",
  "  e.g. sqlite3://overwrites.db?sql=select row, column, value from overwrites order by row, column",
  "",
  "- /path/to/file.csv",
  "  path to CSV file with columns row,col,val (in that order) and rows pre-sorted by row and column",
#endif
  NULL,
};

static int zsv_echo_usage(void) {
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
    zsv_remove(data->tmp_fn);
    free(data->tmp_fn);
  }
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  if (argc < 1 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_echo_usage();
    return 0;
  }
  struct zsv_opts opts = *optsp;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct zsv_echo_data data = {0};
#ifdef ZSV_EXTRAS
  struct zsv_overwrite_opts overwrite_opts = {0};
#endif
  int err = 0;

  // temporary structure for --between parameters
  struct {
    size_t start_row;
    size_t end_row;
  } between = {0};

  for (int arg_i = 1; !err && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
    if (!strcmp(arg, "-b"))
      writer_opts.with_bom = 1;
    else if (!strcmp(arg, "-o"))
      writer_opts.output_path = zsv_next_arg(++arg_i, argc, argv, &err);
    else if (!strcmp(arg, "--contiguous"))
      data.contiguous = 1;
    else if (!strcmp(arg, "--trim-columns"))
      data.trim_columns = 1;
    else if (!strcmp(arg, "--trim"))
      data.trim_white = 1;
    else if (!strcmp(arg, "--skip-until")) {
      if (++arg_i >= argc)
        err = zsv_printerr(1, "Option %s requires a value\n", arg);
      else if (!argv[arg_i][0])
        err = zsv_printerr(1, "--skip-until requires a non-empty value\n");
      else {
        free(data.skip_until_prefix);
        data.skip_until_prefix = (unsigned char *)strdup(argv[arg_i]);
        data.skip_until_prefix_len = data.skip_until_prefix ? strlen((char *)data.skip_until_prefix) : 0;
      }
#ifdef ZSV_EXTRAS
    } else if (!strcmp(arg, "--overwrite")) {
      overwrite_opts.src = zsv_next_arg(++arg_i, argc, argv, &err);
#endif
    } else if (!strcmp(arg, "--end-row") || !strcmp(arg, "--start-row") || !strcmp(arg, "--between-row") ||
               !strcmp(arg, "--between-rows")) {
      const char *val = zsv_next_arg(++arg_i, argc, argv, &err);
      if (!val || !*val || !(atol(val) > 0)) {
        if (!strcmp(arg, "--between-row") || !strcmp(arg, "--between-rows"))
          err = zsv_printerr(1, "%s requires two integer values 0 < N < M\n", arg);
        else
          err = zsv_printerr(1, "%s requires an integer value > 0\n", arg);
      } else if (!strcmp(arg, "--end-row"))
        data.end_row = (size_t)atol(val);
      else if (!strcmp(arg, "--start-row"))
        data.start_row = (size_t)atol(val);
      else if (!strcmp(arg, "--between-row") || !strcmp(arg, "--between-rows")) {
        between.start_row = (size_t)atol(val);
        val = zsv_next_arg(++arg_i, argc, argv, &err);
        if (!val || !*val || !(atol(val) > (long)data.start_row))
          err = zsv_printerr(1, "%s requires two integer values 0 < N < M\n", arg);
        else
          between.end_row = (size_t)atol(val);
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
    } else
      err = zsv_printerr(1, "Unrecognized option: %s\n", arg);
  }

  if (!err && !data.in) {
#ifndef NO_STDIN
    data.in = stdin;
#else
    err = zsv_printerr(1, "Please specify an input file\n");
#endif
  }

  if (!err && between.start_row) {
    if (data.start_row || data.end_row)
      err = zsv_printerr(1, "--between cannot be used together with --start-row or --end-row");
    else {
      data.start_row = between.start_row;
      data.end_row = between.end_row;
    }
  }
  if (data.end_row > 0 && data.end_row < data.start_row)
    err = zsv_printerr(1, "--start-row must be less than --end-row");

  if (err) {
    zsv_echo_cleanup(&data);
    return 1;
  }

  unsigned char buff[4096];

  if (data.start_row)
    opts.row_handler = zsv_echo_row_start_at;
  else if (data.skip_until_prefix)
    opts.row_handler = zsv_echo_row_skip_until;
  else {
    opts.row_handler = zsv_echo_row;
    if (data.trim_columns) {
      // trim columns requires two passes, because we may need to read the entire table
      // to know the maximum number of non-empty columns (e.g. the last row might contain
      // more non-empty columns than the rest of the table

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

      // next, determine the max number of columns from the left that contain data
      struct zsv_opts tmp_opts = opts;
      tmp_opts.row_handler = zsv_echo_get_max_nonempty_cols;
      tmp_opts.stream = data.in;
      tmp_opts.ctx = &data;
      if (zsv_new_with_properties(&tmp_opts, custom_prop_handler, data.input_path, &data.parser) != zsv_status_ok) {
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
  }
  opts.stream = data.in;
  opts.ctx = &data;

  data.csv_writer = zsv_writer_new(&writer_opts);
#ifdef ZSV_EXTRAS
  if (overwrite_opts.src) {
    if (!(opts.overwrite.ctx = zsv_overwrite_context_new(&overwrite_opts)))
      err = zsv_printerr(1, "Out of memory!\n");
    else {
      opts.overwrite.open = zsv_overwrite_open;
      opts.overwrite.next = zsv_overwrite_next;
      opts.overwrite.close = zsv_overwrite_context_delete;
    }
  }
#endif
  if (data.csv_writer && !err) {
    if (zsv_new_with_properties(&opts, custom_prop_handler, data.input_path, &data.parser) != zsv_status_ok)
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
