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
#include <zsv/utils/overwrite.h>
#include <zsv/utils/clock.h>
#include <zsv/utils/overwrite_writer.h>

#define ZSV_COMMAND overwrite
#include "zsv_command.h"

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
  "  -h,--help              : Show this help message",
  "  --old-value <value>    : For `add` or `remove`, only proceed if the old value",
  "                           matches the given value",
  "  --force.               : For `add`, proceed even if an overwrite for the specified",
  "                           cell already exists",
  "                           For `remove`, exit without error even if no overwrite for",
  "                           the specified cell already exists",
  "  --no-timestamp.        : For `add`, don't save timestamp when adding an overwrite",
  "  --all                  : For `remove`, remove all overwrites and delete sqlite file",
  "  --A1                   : For `list`, Display addresses in A1-notation",
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

static int zsv_overwrite_usage(void) {
  for (size_t i = 0; zsv_overwrite_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_overwrite_usage_msg[i]);
  return 1;
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
    if (data->a1) {
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
                               struct zsv_prop_handler *custom_prop_handler) {
  int err = 0;
  if (argc < 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    zsv_overwrite_usage();
    return err;
  }

  struct zsv_overwrite_opts ctx_opts = {0};
  (void)(opts);
  (void)(custom_prop_handler);

  struct zsv_overwrite_args args = {0};
  // By default, save timestamps
  struct zsv_overwrite_data overwrite = {0};
  overwrite.timestamp = (size_t)time(NULL);

  args.filepath = (char *)argv[1];

  for (int i = 2; !err && i < argc; i++) {
    const char *opt = argv[i];
    if (!strcmp(opt, "-f") || !strcmp(opt, "--force")) {
      args.force = 1;
    } else if (!strcmp(opt, "--old-value")) {
      overwrite.old_value.str = (unsigned char *)strdup(argv[++i]);
      overwrite.old_value.len = strlen((const char *)overwrite.old_value.str);
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
        args.bulk_file = (unsigned char *)strdup(argv[++i]);
        args.next = zsv_overwrite_writer_add;
      } else {
        fprintf(stderr, "Expected overwrite filename\n");
        err = 1;
      }
    } else if (!strcmp(opt, "bulk-remove")) {
      if (argc - i > 1) {
        args.mode = zsvsheet_mode_bulk;
        args.bulk_file = (unsigned char *)strdup(argv[++i]);
        args.next = zsv_overwrite_writer_remove;
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

  args.overwrite = &overwrite;

  char *overwrites_fn =
    (char *)zsv_cache_filepath((const unsigned char *)args.filepath, zsv_cache_type_overwrite, 0, 0);
  ctx_opts.src = (char *)overwrites_fn;
  struct zsv_overwrite *data = zsv_overwrite_writer_new(&args, &ctx_opts);
  free(overwrites_fn);

  if (!err && data) {
    if (data->mode == zsvsheet_mode_list)
      err = show_all_overwrites(data, data->writer);
    else if (data->mode == zsvsheet_mode_clear)
      err = zsv_overwrite_writer_clear(data);
    else if (data->mode == zsvsheet_mode_add && data->ctx->sqlite3.db)
      err = zsv_overwrite_writer_add(data);
    else if (data->mode == zsvsheet_mode_remove && data->ctx->sqlite3.db)
      err = zsv_overwrite_writer_remove(data);
    else if (data->mode == zsvsheet_mode_bulk && data->ctx->sqlite3.db) {
      err = zsv_overwrite_writer_bulk(data);
    }
  }

  zsv_overwrite_writer_delete(data);

  return err;
}
