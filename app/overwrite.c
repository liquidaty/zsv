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
#include <zsv/utils/mem.h>

#define ZSV_COMMAND overwrite
#include "zsv_command.h"

enum zsvsheet_mode {
  zsvsheet_mode_add,
  zsvsheet_mode_remove,
  zsvsheet_mode_clear,
  zsvsheet_mode_list,
  zsvsheet_mode_bulk,
};

struct zsv_overwrite_args {
  char *filepath;
  // options
  unsigned char force : 1;
  unsigned char a1 : 1;
  unsigned char all : 1;
  unsigned char *bulk_file;
  // commands
  enum zsvsheet_mode mode;
};

struct zsv_overwrite {
  struct zsv_overwrite_ctx *ctx;
  struct zsv_overwrite_args *args;
  struct zsv_overwrite_data *overwrite;
  int (*next)(struct zsv_overwrite *data);
  // bulk indexes
  size_t row_ix;
  size_t col_ix;
  size_t val_ix;
  size_t old_value_ix;
  size_t timestamp_ix;
  size_t author_ix;
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
  "                             overwrite mydata.csv add B2 \"new value\"",
  "                               - or -",
  "                             overwrite mydata.csv add 1-1 \"new value\"",
  "                           Example 2: change the header in the second column",
  "                           to \"ID #\"",
  "                             overwrite mydata.csv add B1 \"new value\"",
  "                               - or -",
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
  "  --no-timestamp      : For `add`, don't save timestamp when adding an overwrite",
  "  --all               : For `remove`, remove all overwrites and delete sqlite file",
  "  --A1                : For `list`, Display addresses in A1-notation",
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

static int zsv_overwrites_init(struct zsv_overwrite *data) {
  int err = 0;
  if (zsv_mkdirs(data->ctx->src, 1) && !zsv_file_readable(data->ctx->src, &err, NULL)) {
    err = 1;
    perror(data->ctx->src);
    return err;
  }

  sqlite3_stmt *query = NULL;

  /*
  if ((ret = sqlite3_initialize()) != SQLITE_OK) {
    fprintf(stderr, "Failed to initialize library: %d, %s\n", ret, sqlite3_errmsg(ctx->sqlite3.db));
    return err;
  }
  */

  if (sqlite3_open_v2(data->ctx->src, &data->ctx->sqlite3.db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ||
      data->args->mode == zsvsheet_mode_add || data->args->mode == zsvsheet_mode_bulk ||
      data->args->mode == zsvsheet_mode_remove || data->args->mode == zsvsheet_mode_clear) {
    sqlite3_close(data->ctx->sqlite3.db);
    if (sqlite3_open_v2(data->ctx->src, &data->ctx->sqlite3.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) !=
        SQLITE_OK) {
      err = 1;
      fprintf(stderr, "Failed to open conn: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
      return err;
    }

    if (sqlite3_exec(data->ctx->sqlite3.db, "PRAGMA foreign_keys = on", NULL, NULL, NULL) != SQLITE_OK) {
      err = 1;
      fprintf(stderr, "Could not enable foreign keys: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
      return err;
    }

    if (sqlite3_prepare_v2(data->ctx->sqlite3.db,
                           "CREATE TABLE IF NOT EXISTS overwrites ( row integer, column integer, value string, "
                           "timestamp varchar(25), author varchar(25) );",
                           -1, &query, NULL) == SQLITE_OK) {
      if (sqlite3_step(query) != SQLITE_DONE) {
        err = 1;
        fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
        return err;
      }
    } else {
      err = 1;
      fprintf(stderr, "Failed to prepare1: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    }

    if (query)
      sqlite3_finalize(query);

    if (sqlite3_prepare_v2(data->ctx->sqlite3.db, "CREATE UNIQUE INDEX overwrites_uix ON overwrites (row, column)", -1,
                           &query, NULL) == SQLITE_OK) {
      if (sqlite3_step(query) != SQLITE_DONE) {
        err = 1;
        fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
        return err;
      }
      if (query)
        sqlite3_finalize(query);
    }
  }

  if (!data->ctx->sqlite3.db)
    err = 1;

  return err;
}

static int zsv_overwrites_clear(struct zsv_overwrite_ctx *ctx) {
  int err = 0;
  sqlite3_stmt *query = NULL;
  if (sqlite3_prepare_v2(ctx->sqlite3.db, "DELETE FROM overwrites", -1, &query, NULL) == SQLITE_OK) {
    if (sqlite3_step(query) != SQLITE_DONE) {
      err = 1;
      fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(ctx->sqlite3.db));
      return err;
    }
  } else {
    err = 1;
    fprintf(stderr, "Could not prepare: %s\n", sqlite3_errmsg(ctx->sqlite3.db));
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}

static int zsv_overwrites_remove(struct zsv_overwrite *data) {
  int err = 0;
  if (data->args->all) {
    zsv_overwrites_clear(data->ctx);
    return err;
  }

  data->ctx->sqlite3.sql = data->overwrite->old_value.len > 0 ? "DELETE FROM overwrites WHERE row = ? AND column = ? AND value = ?"
                                                 : "DELETE FROM overwrites WHERE row = ? AND column = ?";

  sqlite3_stmt *query = NULL;
  if (sqlite3_prepare_v2(data->ctx->sqlite3.db, data->ctx->sqlite3.sql, -1, &query, NULL) != SQLITE_OK) {
    err = 1;
    if (data->args->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Could not prepare: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    return err;
  }
  sqlite3_bind_int64(query, 1, data->overwrite->row_ix);
  sqlite3_bind_int64(query, 2, data->overwrite->col_ix);
  if (data->overwrite->old_value.len > 0)
    sqlite3_bind_text(query, 3, (const char *)data->overwrite->old_value.str, data->overwrite->old_value.len, SQLITE_STATIC);

  if (sqlite3_step(query) != SQLITE_DONE) {
    err = 1;
    if (data->args->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Could not step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    return err;
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}

static int zsv_overwrites_insert(struct zsv_overwrite *data) {
  if (!data->overwrite->val.str)
    return 1;
  if (data->args->force)
    data->ctx->sqlite3.sql =
      "INSERT OR REPLACE INTO overwrites (row, column, value, timestamp, author) VALUES (?, ?, ?, ?, ?)";
  else if (data->overwrite->old_value.len > 0)
    data->ctx->sqlite3.sql = "INSERT OR REPLACE INTO overwrites (row, column, value, timestamp, author) SELECT ?, ?, "
                             "?, ?, ? WHERE EXISTS (SELECT 1 FROM overwrites WHERE value = ?)";
  else
    data->ctx->sqlite3.sql = "INSERT INTO overwrites (row, column, value, timestamp, author) VALUES (?, ?, ?, ?, ?)";

  int err = 0;
  sqlite3_stmt *query = NULL;

  if (sqlite3_prepare_v2(data->ctx->sqlite3.db, data->ctx->sqlite3.sql, -1, &query, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(query, 1, data->overwrite->row_ix);
    sqlite3_bind_int64(query, 2, data->overwrite->col_ix);
    sqlite3_bind_text(query, 3, (const char *)data->overwrite->val.str, data->overwrite->val.len, SQLITE_STATIC);
    if (data->overwrite->timestamp)
      sqlite3_bind_int64(query, 4, data->overwrite->timestamp);
    else
      sqlite3_bind_null(query, 4);
    if (data->overwrite->author.len > 0)
      sqlite3_bind_text(query, 5, (const char *)data->overwrite->author.str, data->overwrite->author.len, SQLITE_STATIC);
    else
      sqlite3_bind_text(query, 5, "", -1, SQLITE_STATIC);

    if (data->overwrite->old_value.len > 0)
      sqlite3_bind_text(query, 6, (const char *)data->overwrite->old_value.str, data->overwrite->old_value.len, SQLITE_STATIC);

    if (sqlite3_step(query) != SQLITE_DONE) {
      err = 1;
      if (data->args->mode == zsvsheet_mode_bulk)
        sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
      fprintf(stderr, "Value already exists at row %zu and column %zu, use --force to force insert\n",
              data->overwrite->row_ix, data->overwrite->col_ix);
    }
  } else {
    err = 1;
    if (data->args->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Failed to prepare2: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  }

  if (query)
    sqlite3_finalize(query);

  return err;
}

static void zsv_overwrites_bulk(struct zsv_overwrite *data) {
  free(data->ctx->src);
  data->ctx->src = (char*)data->args->bulk_file;
  if(zsv_overwrite_open(data->ctx) != zsv_status_ok) {
      fprintf(stderr, "Could not open\n");
      return;
  }
  data->overwrite->have = 1;
  data->ctx->row_ix = 1;
  if (sqlite3_exec(data->ctx->sqlite3.db, "BEGIN TRANSACTION", NULL, NULL, NULL) == SQLITE_OK) {
    while(data->ctx->next(data->ctx, data->overwrite) == zsv_status_ok && data->overwrite->have) {
      data->next(data);
    }
    if (sqlite3_exec(data->ctx->sqlite3.db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
      fprintf(stderr, "Could not commit changes: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  } else
    fprintf(stderr, "Could not begin transaction: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
}

static int zsv_overwrites_free(struct zsv_overwrite_ctx *ctx, struct zsv_overwrite_data *overwrite,
                               const struct zsv_overwrite_args *args, zsv_csv_writer writer) {
  if (writer)
    zsv_writer_delete(writer);
  if (ctx) {
    sqlite3_close(ctx->sqlite3.db);
    free(ctx->src);
  }
  if (overwrite && args->mode != zsvsheet_mode_bulk)
    free(overwrite->val.str);

  // sqlite3_shutdown();
  return 0;
}

static char *row_col_to_a1(size_t col, size_t row) {
  char buffer[64];
  int index = 63;
  buffer[index] = '\0';

  while (1) {
    if (index == 0)
      return NULL;
    col--;
    buffer[--index] = 'A' + (col % 26);
    col /= 26;
    if (col == 0)
      break;
  }
  // 20 extra bytes for row
  char *result = malloc(strlen(&buffer[index]) + 20 + 1);
  if (result)
    sprintf(result, "%s%zu", &buffer[index], row + 1);
  return result;
}

static int show_all_overwrites(struct zsv_overwrite *data, zsv_csv_writer writer) {
  zsv_writer_cell(writer, 0, (const unsigned char *)"row", 3, 0);
  zsv_writer_cell(writer, 0, (const unsigned char *)"column", 6, 0);
  zsv_writer_cell(writer, 0, (const unsigned char *)"value", 5, 0);
  zsv_writer_cell(writer, 0, (const unsigned char *)"timestamp", 9, 0);
  zsv_writer_cell(writer, 0, (const unsigned char *)"author", 6, 0);
  struct zsv_overwrite_data odata = {.have = 1};
  while ((data->ctx->next(data->ctx, &odata) == zsv_status_ok) && odata.have) {
    if (data->args->a1) {
      char *a1_ix = row_col_to_a1(odata.col_ix + 1, odata.row_ix);
      if (a1_ix)
        zsv_writer_cell(writer, 1, (unsigned char *)a1_ix, strlen(a1_ix), 0);
      free(a1_ix);
    } else {
      zsv_writer_cell_zu(writer, 1, odata.row_ix);
      zsv_writer_cell_zu(writer, 0, odata.col_ix);
    }
    zsv_writer_cell(writer, 0, odata.val.str, odata.val.len, 0);
    if (odata.timestamp)
      zsv_writer_cell_zu(writer, 0, odata.timestamp);
    else
      zsv_writer_cell(writer, 0, (unsigned char *)"", 0, 0);
    if (odata.author.len > 0 && odata.author.str)
      zsv_writer_cell(writer, 0, odata.author.str, odata.author.len, 0);
    else
      zsv_writer_cell(writer, 0, (unsigned char *)"", 0, 0);
  }
  return 0;
}

struct xl_address {
  size_t row, col;
};

static int parse_xl_address(const char *s, struct xl_address *address) {
  memset(address, 0, sizeof(*address));
  unsigned len = strlen(s);
  for (unsigned i = 0; i < len; i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      if (address->col == 0)
        return 1; // alpha should come before num
      if (address->row == 0 && c == '0')
        return 1; // first num should not be zero
      address->row = address->row * 10 + (c - '0');
    } else if (c >= 'A' && c <= 'Z') {
      if (address->row > 0)
        return 1; // alpha should come before num
      address->col = address->col * 26 + (c - 'A') + 1;
    } else if (c >= 'a' && c <= 'z') {
      if (address->row > 0)
        return 1; // alpha should come before num
      address->col = address->col * 26 + (c - 'a') + 1;
    } else
      break;
  }
  if (address->row > 0 && address->col > 0) {
    address->row--;
    address->col--;
    return 0;
  }
  return 1; // error
}

static int zsv_overwrite_parse_pos(struct zsv_overwrite_data *overwrite, const char *str) {
  // this means it's an excel-style cell, because it does not start with a number for the row
  if (!isdigit(*str)) {
    struct xl_address address;
    if (parse_xl_address(str, &address) != 0)
      return 1;
    overwrite->row_ix = address.row;
    overwrite->col_ix = address.col;
    return 0;
  }
  return sscanf(str, "%zu-%zu", &overwrite->row_ix, &overwrite->col_ix) != 2;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  int err = 0;
  if (argc < 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    zsv_overwrite_usage();
    return err;
  }

  struct zsv_overwrite_opts ctx_opts = {0};
  (void)(opts);
  (void)(custom_prop_handler);
  (void)(opts_used);
  struct zsv_overwrite *data = calloc(1, sizeof(struct zsv_overwrite));
  struct zsv_overwrite_args args = {0};
  // By default, save timestamps
  struct zsv_overwrite_data overwrite = {0};
  overwrite.timestamp = (size_t)time(NULL);
  struct zsv_csv_writer_options writer_opts = {0};

  char *filepath = (char *)argv[1];
  args.filepath = filepath;

  for (int i = 2; !err && i < argc; i++) {
    const char *opt = argv[i];
    if (!strcmp(opt, "-f") || !strcmp(opt, "--force")) {
      args.force = 1;
    } else if (!strcmp(opt, "--old-value")) {
      overwrite.old_value.str = (unsigned char *)argv[++i];
      overwrite.old_value.len = strlen((const char*)overwrite.old_value.str);
    } else if (!strcmp(opt, "--no-timestamp")) {
      overwrite.timestamp = 0;
    } else if (!strcmp(opt, "--A1")) {
      args.a1 = 1;
    } else if (!strcmp(opt, "list")) {
      args.mode = zsvsheet_mode_list;
    } else if (!strcmp(opt, "clear")) {
      args.mode = zsvsheet_mode_clear;
    } else if (!strcmp(opt, "add")) {
      if (argc - i > 2) {
        args.mode = zsvsheet_mode_add;
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
    } else if (!strcmp(opt, "remove")) {
      if (argc - i > 1) {
        args.mode = zsvsheet_mode_remove;
        if (!strcmp(argv[++i], "--all"))
          args.all = 1;
        else
          err = zsv_overwrite_parse_pos(&overwrite, argv[i]);
        if (err) {
          fprintf(stderr, "Expected row and column\n");
        }
      } else {
        fprintf(stderr, "Expected row and column\n");
        err = 1;
      }
    } else if (!strcmp(opt, "bulk-add")) {
      if (argc - i > 1) {
        args.mode = zsvsheet_mode_bulk;
        args.bulk_file = (unsigned char*)strdup(argv[++i]);
        data->next = zsv_overwrites_insert;
      } else {
        fprintf(stderr, "Expected overwrite filename\n");
        err = 1;
      }
    } else if (!strcmp(opt, "bulk-remove")) {
      if (argc - i > 1) {
        args.mode = zsvsheet_mode_bulk;
        args.bulk_file = (unsigned char*)strdup(argv[++i]);
        data->next = zsv_overwrites_remove;
      } else {
        fprintf(stderr, "Expected overwrite filename\n");
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

  char *overwrites_fn = (char *)zsv_cache_filepath((const unsigned char *)filepath, zsv_cache_type_overwrite, 0, 0);
  ctx_opts.src = (char *)overwrites_fn;

  struct zsv_overwrite_ctx *ctx = zsv_overwrite_context_new(&ctx_opts);
  free(overwrites_fn);

  data->ctx = ctx;
  data->args = &args;
  data->overwrite = &overwrite;

  if (args.mode == zsvsheet_mode_list) {
    if ((err = (zsv_overwrite_open(data->ctx) != zsv_status_ok)))
      fprintf(stderr, "Failed to initalize database\n");
  } else {
    if ((err = zsv_overwrites_init(data)))
      fprintf(stderr, "Failed to initalize database\n");
  }

  zsv_csv_writer writer = zsv_writer_new(&writer_opts);

  if (err) {
    zsv_overwrites_free(ctx, &overwrite, &args, writer);
    return err;
  }

  if (args.mode == zsvsheet_mode_list)
    show_all_overwrites(data, writer);
  else if (args.mode == zsvsheet_mode_clear)
    zsv_overwrites_clear(ctx);
  else if (args.mode == zsvsheet_mode_add && ctx->sqlite3.db)
    zsv_overwrites_insert(data);
  else if (args.mode == zsvsheet_mode_remove && ctx->sqlite3.db)
    zsv_overwrites_remove(data);
  else if (args.mode == zsvsheet_mode_bulk && ctx->sqlite3.db) {
    zsv_overwrites_bulk(data);
  }

  if (args.mode == zsvsheet_mode_list || args.mode == zsvsheet_mode_bulk) {
    zsv_overwrite_context_delete(data->ctx);
    zsv_writer_delete(writer);
  } else
    zsv_overwrites_free(ctx, &overwrite, &args, writer);

  if (data->args->all)
    remove(ctx->src);
  if (data)
    free(data);

  return err;
}
