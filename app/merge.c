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

#define ZSV_COMMAND merge
#include "zsv_command.h"

#include <zsv/utils/compiler.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/string.h>

struct zsv_merge_data {
  FILE *in;
  const char *input_path;
  zsv_csv_writer csv_writer;
  zsv_parser parser;
  size_t row_ix;

  struct {
    size_t row_ix;
    size_t col_ix;
    const unsigned char *str;
    size_t len;
    char eof;
  } override;

  char override_input_type; // reserved
  union {
    struct {
      sqlite3 *db;
      sqlite3_stmt *stmt; // select row, column, override
    } sqlite3;
  } o;
};

/**
 * check if sqlite3 statement is valid
 * return error
 */
static int zsv_merge_sqlite3_check_stmt(sqlite3_stmt *stmt) {
  if(sqlite3_column_count(stmt) < 3)
    return 1;
  // to do: check that columns are row, column, value (or override)
  return 0;
}

/**
 * to do: verify original value
 */
void zsv_merge_get_next_override(struct zsv_merge_data *data) {
  if(!data->override.eof) {
    sqlite3_stmt *stmt = data->o.sqlite3.stmt;
    if(stmt) {
      if(sqlite3_step(stmt) == SQLITE_ROW) {
        // row, column, value
        data->override.row_ix = sqlite3_column_int64(stmt, 0);
        data->override.col_ix = sqlite3_column_int64(stmt, 1);
        data->override.str = sqlite3_column_text(stmt, 2);
        data->override.len = sqlite3_column_bytes(stmt, 2);
      } else
        data->override.eof = 1;
    }
  }
}

static void zsv_merge_row(void *hook) {
  struct zsv_merge_data *data = hook;
  if(VERY_UNLIKELY(data->row_ix == 0)) { // header
    for(size_t i = 0, j = zsv_cell_count(data->parser); i < j; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      zsv_writer_cell(data->csv_writer, i == 0, cell.str, cell.len, cell.quoted);
    }
  } else {
    for(size_t i = 0, j = zsv_cell_count(data->parser); i < j; i++) {
      if(data->override.row_ix == data->row_ix && data->override.col_ix == i) {
        zsv_writer_cell(data->csv_writer, i == 0, data->override.str, data->override.len, 1);
        zsv_merge_get_next_override(data);
      } else {
        struct zsv_cell cell = zsv_get_cell(data->parser, i);
        zsv_writer_cell(data->csv_writer, i == 0, cell.str, cell.len, cell.quoted);
      }
    }
    while(!data->override.eof && data->override.row_ix <= data->row_ix)
      zsv_merge_get_next_override(data);
  }
  data->row_ix++;
}

const char *zsv_merge_usage_msg[] =
  {
   APPNAME ": Merge two tabular data files",
   "",
   "Usage: " APPNAME " file1 overrides.(db|csv) [--sql <query>]",
   "merges two files",
   "",
   "Options:",
   "  -b        : output with BOM",
   "  --override: xxx",
   "  --sql     : xxx",
   NULL
  };

static int zsv_merge_usage() {
  for(int i = 0; zsv_merge_usage_msg[i]; i++)
    fprintf(stderr, "%s\n", zsv_merge_usage_msg[i]);
  return 1;
}

static void zsv_merge_cleanup(struct zsv_merge_data *data) {
  zsv_writer_delete(data->csv_writer);
  if(data->o.sqlite3.stmt)
    sqlite3_finalize(data->o.sqlite3.stmt);
  if(data->in && data->in != stdin)
    fclose(data->in);
  if(data->o.sqlite3.db)
    sqlite3_close(data->o.sqlite3.db);
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, const char *opts_used) {
  if(argc < 2 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_merge_usage();
    return 0;
  }

  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  struct zsv_merge_data data = { 0 };

  int err = 0;

  const char *input_path = argv[1];
#ifndef NO_STDIN
  if(!strcmp(input_path, "-"))
    data.in = stdin;
#endif
  if(!data.in) {
    if(!(data.in = fopen(input_path, "rb"))) {
      err = 1;
      perror(input_path);
    } else
      data.input_path = input_path;
  }

  if(data.in && !err) {
    const char *sqlite3_filename = argv[2];
    int rc = sqlite3_open_v2(sqlite3_filename, &data.o.sqlite3.db, SQLITE_OPEN_READONLY, NULL);
    if(rc != SQLITE_OK || !data.o.sqlite3.db)
      err = 1;

    if(!err) {
      rc = sqlite3_prepare_v2(data.o.sqlite3.db, "select row, column, override as value from gxfile_data_override", -1, &data.o.sqlite3.stmt, NULL);
      if(rc != SQLITE_OK || !data.o.sqlite3.stmt)
        err = 1;
    }

    for(int arg_i = 1; !err && arg_i < argc; arg_i++) {
      const char *arg = argv[arg_i];
      if(!strcmp(arg, "-b"))
        writer_opts.with_bom = 1;
    }
  }

  if(err) {
    zsv_merge_cleanup(&data);
    return 1;
  }

  opts->row_handler = zsv_merge_row;
  opts->stream = data.in;
  opts->ctx = &data;
  data.csv_writer = zsv_writer_new(&writer_opts);
  if(zsv_new_with_properties(opts, input_path, opts_used, &data.parser) != zsv_status_ok
     || !data.csv_writer) {
    zsv_merge_cleanup(&data);
    return 1;
  }

  // create a local csv writer buff for faster performance
  unsigned char writer_buff[64];
  zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

  // process the input data.
  zsv_handle_ctrl_c_signal();
  enum zsv_status status;
  while(!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
    ;

  zsv_finish(data.parser);
  zsv_delete(data.parser);
  zsv_merge_cleanup(&data);
  return 0;
}
