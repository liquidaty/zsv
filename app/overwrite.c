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
#include <zsv/utils/cache.h>
#include <zsv/utils/os.h>
#include <zsv/utils/file.h>
#include <zsv/utils/overwrite.h>

#define ZSV_COMMAND overwrite
#include "zsv_command.h"

struct zsv_overwrite_ctx {
  unsigned const char *filepath;
  sqlite3 *db;

  struct zsv_overwrite_data overwrite;

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
  "                             overwrite add A1 \"new value\"",
  "                               - or -",
  "                             overwrite add 1-1 \"new value\"",
  "                           Example 2: change the header in the first column",
  "                           to \"ID #\"",
  "                             overwrite add 0-1 \"ID #\"",
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

static int zsv_overwrites_init(struct zsv_overwrite_ctx *ctx) {
  const unsigned char *filepath = ctx->filepath;
  const char *overwrites_fn = (const char *)zsv_cache_filepath(filepath, zsv_cache_type_overwrite, 0, 0);

  int err = 0;
  if (zsv_mkdirs(overwrites_fn, 1) && !zsv_file_readable(overwrites_fn, &err, NULL)) {
    err = 1;
    perror(overwrites_fn);
    return err;
  }

  sqlite3_stmt *query = NULL;
  int ret = 0;

  if ((ret = sqlite3_initialize()) != SQLITE_OK) {
    fprintf(stderr, "Failed to initialize library: %d\n", ret);
    return err;
  }

  if ((ret = sqlite3_open_v2(overwrites_fn, &ctx->db, SQLITE_OPEN_READONLY, NULL)) != SQLITE_OK || ctx->add ||
      ctx->clear) {
    sqlite3_close(ctx->db);
    if ((ret = sqlite3_open_v2(overwrites_fn, &ctx->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) !=
        SQLITE_OK) {
      err = 1;
      fprintf(stderr, "Failed to open conn: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
      return err;
    }

    if ((ret = sqlite3_prepare_v2(ctx->db,
                                  "CREATE TABLE IF NOT EXISTS overwrites ( row integer, col integer, orig_val string, val string );", -1,
                                  &query, NULL)) == SQLITE_OK) {
      if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
        err = 1;
        fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
        return err;
      }
    } else {
      err = 1;
      fprintf(stderr, "Failed to prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
    }
    if (query)
      sqlite3_finalize(query);
  }

  if (!ctx->db)
    err = 1;

  return err;
}

static int zsv_overwrites_clear(struct zsv_overwrite_ctx *ctx) {
  int err = 0;
  sqlite3_stmt *query = NULL;
  int ret;
  if ((ret = sqlite3_prepare_v2(ctx->db, "DELETE FROM overwrites", -1, &query, NULL)) == SQLITE_OK) {
    if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
      err = 1;
      fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
      return err;
    }
  } else {
    err = 1;
    fprintf(stderr, "Could not prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}

static int zsv_overwrites_insert(struct zsv_overwrite_ctx *ctx) {
  int err = 0;
  sqlite3_stmt *query = NULL;
  int ret;
  if ((ret = sqlite3_prepare_v2(ctx->db, "INSERT INTO overwrites (row, col, orig_val, val) VALUES (?, ?, ?, ?)", -1, &query,
                                NULL)) == SQLITE_OK) {
    sqlite3_bind_int(query, 1, (int)ctx->overwrite.row_ix);
    sqlite3_bind_int(query, 2, (int)ctx->overwrite.col_ix);
    sqlite3_bind_text(query, 3, (const char *)ctx->overwrite.val.str, -1, SQLITE_STATIC);
    if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
      err = 1;
      fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
      return err;
    }
  } else {
    err = 1;
    fprintf(stderr, "Failed to prepare: %d, %s\n", ret, sqlite3_errmsg(ctx->db));
  }

  if (query)
    sqlite3_finalize(query);

  return err;
}

static int zsv_overwrites_exit(struct zsv_overwrite_ctx *ctx) {
  sqlite3_close(ctx->db);
  sqlite3_shutdown();
  return 0;
}

static int show_all_overwrites(struct zsv_overwrite_ctx *ctx) {
  int err = 0;
  sqlite3_stmt *stmt;
  int ret;
  if ((ret = sqlite3_prepare_v2(ctx->db, "SELECT * FROM overwrites", -1, &stmt, NULL) != SQLITE_OK)) {
    err = 1;
    fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(ctx->db));
    return err;
  }

  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
    size_t row = sqlite3_column_int(stmt, 0);
    size_t col = sqlite3_column_int(stmt, 1);
    const unsigned char *name = sqlite3_column_text(stmt, 2); // Column 1

    printf("row: %zu, col: %zu, name: %s\n", row, col, name ? (const char *)name : "NULL");
  }

  if (ret != SQLITE_DONE) {
    fprintf(stderr, "Error during fetching rows: %s\n", sqlite3_errmsg(ctx->db));
  }

  if (stmt)
    sqlite3_finalize(stmt);

  return err;
}

static int zsv_overwrite_parse_pos(struct zsv_overwrite_ctx *ctx, const char *str) {
  return sscanf(str, "%zu-%zu", &ctx->overwrite.row_ix, &ctx->overwrite.col_ix) != 2;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  int err = 0;
  if (argc < 3 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_overwrite_usage();
    return err;
  }

  struct zsv_overwrite_ctx ctx = {0};

  const unsigned char *filepath = (const unsigned char *)argv[1];
  ctx.filepath = filepath;

  for (int i = 2; !err && i < argc; i++) {
    const char *opt = argv[i];
    if (!strcmp(opt, "-f") || !strcmp(opt, "--force")) {
      err = 1;
      fprintf(stderr, "Error: %s is not implemented\n", opt);
    } else if (!strcmp(opt, "--old-value")) {
      fprintf(stderr, "Error: %s is not implemented\n", opt);
      err = 1;
    } else if (!strcmp(opt, "list")) {
      ctx.list = 1;
    } else if (!strcmp(opt, "clear")) {
      ctx.clear = 1;
    } else if (!strcmp(opt, "add")) {
      if (argc - i > 2) {
        ctx.add = 1;
        err = zsv_overwrite_parse_pos(&ctx, argv[++i]);
        ctx.overwrite.val.str = (unsigned char *)strdup(argv[++i]);
        ctx.overwrite.val.len = strlen((const char *)ctx.overwrite.val.str);
        if (err || !ctx.overwrite.val.str) {
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

  if ((err = zsv_overwrites_init(&ctx))) {
    fprintf(stderr, "Failed to initalize database\n");
  }

  if (ctx.list)
    show_all_overwrites(&ctx);
  else if (ctx.clear)
    zsv_overwrites_clear(&ctx);
  else if (!err && ctx.add && ctx.db)
    zsv_overwrites_insert(&ctx);

  zsv_overwrites_exit(&ctx);

  return err;
}
