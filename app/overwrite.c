/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>

#define _GNU_SOURCE 1
#include <string.h>
#include <ctype.h>

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/os.h>
#include <zsv/utils/file.h>
#include <zsv/utils/overwrite.h>
#include <zsv/utils/clock.h>

#define ZSV_COMMAND overwrite
#include "zsv_command.h"

struct zsv_overwrite_args {
  char *filepath;
  unsigned char list : 1;
  unsigned char clear : 1;
  unsigned char add : 1;
};

const char *zsv_overwrite_usage_msg[] = {
  APPNAME " - Manage overwrites associated with a CSV file",
  "",
  "Usage:",
  "  " APPNAME " <file> <command> [arguments] [options]",
  "",
  "Commands (where <cell> can be <row>-<col> or an Excel-style address):",
  "  list                   : Display all saved overwrite entries",
  "  add <cell> <value>     : Add an overwrite entry",
  "                           Example 1: overwrite the first column of the first",
  "                           non-header row",
  "                             overwrite mydata.csv add A1 \"new value\"",
  "                               - or -",
  "                             overwrite mydata.csv add 1-1 \"new value\"",
  "                           Example 2: change the header in the first column",
  "                           to \"ID #\"",
  "                             overwrite mydata.csv add 0-1 \"ID #\"",
  "  remove <cell>          : Remove an overwrite entry",
  "  clear                  : Remove any / all overwrites",
  "  bulk-add <datafile>    : Bulk add overwrite entries from a CSV or JSON file",
  "  bulk-remove <datafile> : Bulk remove overwrite entries from a CSV or JSON file",
  "",
  "Options:",
  "  -h,--help           : Show this help message",
  "  --old-value <value> : For `add` or `remove`, only proceed if the old value",
  "                        matches the given value",
  "  --force             : For `add`, proceed even if an overwrite for the specified",
  "                        cell already exists",
  "                        For `remove`, exit without error even if no overwrite for",
  "                        the specified cell already exists",
  "",
  "Description:",
  "  The  `overwrite`  utility  allows  you to manage a list of \"overwrites\" associated",
  "  with a given CSV input file. Each overwrite entry is a tuple consisting  of  row,",
  "  column,  original  value, and new value, along with optional timestamp and author",
  "  metadata.",
  "",
  "  Overwrite data for a given input file `/path/to/my-data.csv` is stored in the \"over‐",
  "  writes\"  table  of  `/path/to/.zsv/data/my-data.csv/overwrite.sqlite3`.",
  "",
  "  For bulk operations, the data file must be a CSV with \"row\", \"col\" and \"value\" columns",
  "  and may optionally include \"old value\", \"timestamp\" and/or \"author\"",
  NULL};

static int zsv_overwrite_usage() {
  for (size_t i = 0; zsv_overwrite_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_overwrite_usage_msg[i]);
  return 1;
}

static int zsv_overwrites_init(struct zsv_overwrite_ctx *ctx, struct zsv_overwrite_args *args) {
  char *filepath = args->filepath;
  const char *overwrites_fn =
    (const char *)zsv_cache_filepath((const unsigned char *)filepath, zsv_cache_type_overwrite, 0, 0);
  ctx->src = (char *)overwrites_fn;

  int err = 0;
  if (zsv_mkdirs(overwrites_fn, 1) && !zsv_file_readable(overwrites_fn, &err, NULL)) {
    err = 1;
    perror(overwrites_fn);
    return err;
  }

  sqlite3_stmt *query = NULL;
  int ret = 0;

  if ((ret = sqlite3_initialize()) != SQLITE_OK) {
    fprintf(stderr, "Failed to initialize library: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
    return err;
  }

  if ((ret = sqlite3_open_v2(overwrites_fn, &ctx->sqlite3.db, SQLITE_OPEN_READONLY, NULL)) != SQLITE_OK || args->add ||
      args->clear) {
    sqlite3_close(ctx->sqlite3.db);
    if ((ret = sqlite3_open_v2(overwrites_fn, &ctx->sqlite3.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) !=
        SQLITE_OK) {
      err = 1;
      fprintf(stderr, "Failed to open conn: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
      return err;
    }

    if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db,
                                  "CREATE TABLE IF NOT EXISTS overwrites ( row integer, column integer, value string, "
                                  "timestamp varchar(25), author varchar(25) );",
                                  -1, &query, NULL)) == SQLITE_OK) {
      if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
        err = 1;
        fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
        return err;
      }
    } else {
      err = 1;
      fprintf(stderr, "Failed to prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
    }

    if (query)
      sqlite3_finalize(query);

    if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db, "CREATE UNIQUE INDEX overwrites_uix ON overwrites (row, column)", -1,
                                  &query, NULL)) == SQLITE_OK) {
      if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
        err = 1;
        fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
        return err;
      }
      if (query)
        sqlite3_finalize(query);
    }
  }

  if (!ctx->sqlite3.db)
    err = 1;

  return err;
}

static int zsv_overwrites_clear(struct zsv_overwrite_ctx *ctx) {
  int err = 0;
  sqlite3_stmt *query = NULL;
  int ret;
  if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db, "DELETE FROM overwrites", -1, &query, NULL)) == SQLITE_OK) {
    if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
      err = 1;
      fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
      return err;
    }
  } else {
    err = 1;
    fprintf(stderr, "Could not prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}

static int zsv_overwrite_check_for_value(struct zsv_overwrite_ctx *ctx, struct zsv_overwrite_data *overwrite) {
  sqlite3_stmt *query = NULL;
  int ret = 0;
  int err = 0;
  if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db, "SELECT 1 FROM overwrites WHERE row = ? AND column = ?", -1, &query,
                                NULL)) != SQLITE_OK) {
    err = 1;
    fprintf(stderr, "Failed to prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
    return err;
  }
  sqlite3_bind_int64(query, 1, overwrite->row_ix);
  sqlite3_bind_int64(query, 2, overwrite->col_ix);
  if ((ret = sqlite3_step(query)) == SQLITE_ROW) // Value exists
    err = 1;
  else // Value does not exist
    err = 0;

  if (query)
    sqlite3_finalize(query);
  return err;
}

static int zsv_overwrites_insert(struct zsv_overwrite_ctx *ctx, struct zsv_overwrite_data *overwrite) {
  int err = 0;
  sqlite3_stmt *query = NULL;
  int ret;

  if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db,
                                "INSERT INTO overwrites (row, column, value, timestamp, author) VALUES (?, ?, ?, ?, ?)",
                                -1, &query, NULL)) == SQLITE_OK) {
    sqlite3_bind_int64(query, 1, overwrite->row_ix);
    sqlite3_bind_int64(query, 2, overwrite->col_ix);
    sqlite3_bind_text(query, 3, (const char *)overwrite->val.str, -1, SQLITE_STATIC);
    time_t my_time = time(NULL);
    char *time_buf = ctime(&my_time);
    time_buf[strlen(time_buf) - 1] = '\0'; // Remove newline
    sqlite3_bind_text(query, 4, (const char *)time_buf, -1, SQLITE_STATIC);
    sqlite3_bind_text(query, 5, "", -1, SQLITE_STATIC); // author
    if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
      err = 1;
      fprintf(stderr, "Value already exists at row %zu and column %zu, use --force to force insert\n",
              overwrite->row_ix, overwrite->col_ix);
      return err;
    }
  } else {
    err = 1;
    fprintf(stderr, "Failed to prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
  }

  if (query)
    sqlite3_finalize(query);

  return err;
}

static int zsv_overwrites_exit(struct zsv_overwrite_ctx *ctx, struct zsv_overwrite_data *overwrite, zsv_csv_writer writer) {
  zsv_writer_delete(writer);
  free(overwrite->val.str);
  sqlite3_close(ctx->sqlite3.db);
  sqlite3_shutdown();
  return 0;
}

static int show_all_overwrites(struct zsv_overwrite_ctx *ctx, zsv_csv_writer writer) {
  int err = 0;
  sqlite3_stmt *stmt;
  int ret;
  if ((ret = sqlite3_prepare_v2(ctx->sqlite3.db, "SELECT * FROM overwrites", -1, &stmt, NULL) != SQLITE_OK)) {
    err = 1;
    fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(ctx->sqlite3.db));
    return err;
  }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
    size_t row = sqlite3_column_int64(stmt, 0);
    size_t col = sqlite3_column_int64(stmt, 1);
    const unsigned char *val = sqlite3_column_text(stmt, 2);
    size_t val_len = sqlite3_column_bytes(stmt, 2);
    const unsigned char *timestamp = sqlite3_column_text(stmt, 3);
    size_t timestamp_len = sqlite3_column_bytes(stmt, 3);
    const unsigned char *author = sqlite3_column_text(stmt, 4);
    size_t author_len = sqlite3_column_bytes(stmt, 4);


    zsv_writer_cell_zu(writer, 1, row);
    zsv_writer_cell_zu(writer, 0, col);
    zsv_writer_cell(writer, 0, val, val_len, 0);
    zsv_writer_cell(writer, 0, timestamp, timestamp_len, 0);
    zsv_writer_cell(writer, 0, author, author_len, 0);
  }

  if (ret != SQLITE_DONE) {
    fprintf(stderr, "Error during fetching rows: %s\n", sqlite3_errmsg(ctx->sqlite3.db));
  }

  if (stmt)
    sqlite3_finalize(stmt);

  return err;
}

static int zsv_overwrite_parse_pos(struct zsv_overwrite_data *overwrite, const char *str) {
  return sscanf(str, "%zu-%zu", &overwrite->row_ix, &overwrite->col_ix) != 2;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  int err = 0;
  if (argc < 3 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_overwrite_usage();
    return err;
  }

  struct zsv_overwrite_ctx ctx = {0};
  struct zsv_overwrite_args args = {0};
  struct zsv_overwrite_data overwrite = {0};
  struct zsv_csv_writer_options writer_opts = {0};

  char *filepath = (char *)argv[1];
  args.filepath = filepath;

  for (int i = 2; !err && i < argc; i++) {
    const char *opt = argv[i];
    if (!strcmp(opt, "-f") || !strcmp(opt, "--force")) {
      err = 1;
      fprintf(stderr, "Error: %s is not implemented\n", opt);
    } else if (!strcmp(opt, "--old-value")) {
      fprintf(stderr, "Error: %s is not implemented\n", opt);
      err = 1;
    } else if (!strcmp(opt, "list")) {
      args.list = 1;
    } else if (!strcmp(opt, "clear")) {
      args.clear = 1;
    } else if (!strcmp(opt, "add")) {
      if (argc - i > 2) {
        args.add = 1;
        err = zsv_overwrite_parse_pos(&overwrite, argv[++i]);
        overwrite.val.str = (unsigned char *)strdup(argv[++i]);
        overwrite.val.len = strlen((const char *)overwrite.val.str);
        if (err || !overwrite.val.str) {
          fprintf(stderr, "Expected row, column, and value\n");
          err = 1;
        }
      } else {
        fprintf(stderr, "Expected row, column, and value\n");
        err = 1;
      }
    } else {
      err = 1;
      if (*opt == '-')
        fprintf(stderr, "Unrecognized option: %s\n", opt);
      else
        fprintf(stderr, "Unrecognized command or argument: %s\n", opt);
    }
  }

  if (err)
    return err;

  if ((err = zsv_overwrites_init(&ctx, &args))) {
    fprintf(stderr, "Failed to initalize database\n");
  }

  zsv_csv_writer writer = zsv_writer_new(&writer_opts);
  if (args.list)
    show_all_overwrites(&ctx, writer);
  else if (args.clear)
    zsv_overwrites_clear(&ctx);
  else if (!err && args.add && ctx.sqlite3.db)
    zsv_overwrites_insert(&ctx, &overwrite);

  zsv_overwrites_exit(&ctx, &overwrite, writer);

  return err;
}
