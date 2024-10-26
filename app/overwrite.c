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

const char *zsv_overwrite_usage_msg[] = {
  APPNAME ": save, modify or apply overwrites",
  "",
  "Usage:",
  "  " APPNAME " <filename>: display saved overwrites",
  "  " APPNAME " <filename> <row> <column> <value>: save a value at the given row and co,lumn",
  "    row and column should be 1-based (the first row = 1, the first column = 1)",
  "  " APPNAME " <filename> <address> <value>: save a value at the given Excel-style address",
  "    for example, the address of the cell at the second column of the sixth row is B6",
  "  " APPNAME " <filename> remove-all           : remove all overwrites",
  "  " APPNAME " <filename> remove <row> <column>",
  "  " APPNAME " <filename> remove <address>     : remove an overwrite at the given location",
  "  " APPNAME " <filename> bulk <filename>       : bulk save from csv file contents",
  "  " APPNAME " <filename> bulk-remove <filename>: bulk remove from csv file contents",
  "",
  "",
  "options:",
  "--author <value>: add the specified author to the overwrite record",
  "--timestamp <value>: add the specified timestamp to the overwrite record",
  "--reason <value>: add the specified reason to the overwrite record",
  "-f,--force: replace prior overwrite in the same address",
  "",
  "Notes: overwrite data relating to /path/to/my-data.csv",
  "is kept in the \"overwrites\" table of /path/to/.zsv/data/my-data.csv/overwrite.sqlite3.",
  NULL};

static int zsv_overwrite_usage() {
  for (size_t i = 0; zsv_overwrite_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_overwrite_usage_msg[i]);
  return 1;
}

static sqlite3 *zsv_overwrites_init(const unsigned char *filepath) {
  const char *overwrites_fn = (const char *)zsv_cache_filepath(filepath, zsv_cache_type_overwrite, 0, 0);

  int err = 0;
  if (!zsv_file_readable(overwrites_fn, &err, NULL) && zsv_mkdirs(overwrites_fn, 1)) {
    perror(overwrites_fn);
    return NULL;
  }

  sqlite3 *db = NULL;
  sqlite3_stmt *query = NULL;
  int ret = 0;

  if ((ret = sqlite3_initialize()) != SQLITE_OK) {
    fprintf(stderr, "Failed to initialize library: %d\n", ret);
    return NULL;
  }

  if ((ret = sqlite3_open_v2(overwrites_fn, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) != SQLITE_OK) {
    fprintf(stderr, "Failed to open conn: %d, %s\n", ret, sqlite3_errmsg(db));
    return NULL;
  }

  if ((ret = sqlite3_prepare_v2(db, "CREATE TABLE IF NOT EXISTS overwrites ( row integer, col integer, val string );",
                                -1, &query, NULL)) == SQLITE_OK) {
    if ((ret = sqlite3_step(query)) != SQLITE_DONE) {
      fprintf(stderr, "Failed to step: %d, %s\n", ret, sqlite3_errmsg(db));
      return NULL;
    }
  }
  if (query)
    sqlite3_finalize(query);

  return db;
}

static int zsv_overwrites_exit(sqlite3 *db) {
  sqlite3_close(db);
  sqlite3_shutdown();
  return 0;
}

static int show_all_overwrites(const unsigned char *filepath) {
  // TODO: read from overwrites sql file and display all overwrites
  printf("Showing all overwrites is not implemented\n");

  int err = 0;
  return err;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  int err = 0;
  if (argc < 2 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    err = 1;
    zsv_overwrite_usage();
    return err;
  }

  const unsigned char *filepath = (const unsigned char *)argv[1];

  size_t row = 0;
  size_t col = 0;
  const char *val = NULL;

  if (argc == 2)
    return show_all_overwrites(filepath);

  for (int i = 1; !err && i < argc; i++) {
    const char *opt = argv[i];
    if (!strcmp(opt, "-f") || !strcmp(opt, "--force")) {
      err = 1;
      fprintf(stderr, "Error: %s is not implemented\n", opt);
    } else if (!strcmp(opt, "--author")) {
      fprintf(stderr, "Error: %s is not implemented\n", opt);
      err = 1;
    } else if (!strcmp(opt, "--reason")) {
      fprintf(stderr, "Error: %s is not implemented\n", opt);
      err = 1;
    } else if (!strcmp(opt, "--timestamp")) {
      fprintf(stderr, "Error: %s is not implemented\n", opt);
      err = 1;
    } else {
      if (*opt == '-') {
        err = 1;
        fprintf(stderr, "Unrecognized option: %s\n", opt);
      }
      if (!row)
        row = atoi(opt);
      else if (!col)
        col = atoi(opt);
      else
        val = strdup(opt);
    }
  }

  if (err)
    return err;

  sqlite3 *db = NULL;
  if (!(db = zsv_overwrites_init(filepath))) {
    fprintf(stderr, "Failed to initalize database\n");
  }

  if (err)
    fprintf(stderr, "Failed to initialize database\n");

  if (!err && row && col && val && db)
    printf("Found row %zu, column %zu, and value %s for overwrite\n", row, col, val);

  zsv_overwrites_exit(db);

  return err;
}
