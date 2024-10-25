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

#define _GNU_SOURCE 1
#include <string.h>
#include <ctype.h>

#include <sqlite3.h>

#include <zsv.h>
#include <zsv/utils/cache.h>
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
  "is kept in the \"overwrites\" table of /path/to/.zsv/data/my-data.csv/overwrite.sqlite3."};

static int zsv_overwrite_usage() {
  for (size_t i = 0; zsv_overwrite_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_overwrite_usage_msg[i]);
  return 1;
}

static int show_all_overwrites(const unsigned char *filepath) {
  int err = 0;

  if (!zsv_file_readable((const char *)filepath, &err, NULL)) {
    perror((const char *)filepath);
    return err;
  }

  // TODO: read from overwrites file

  printf("Overwrites not implemented\n");

  if (err == ENOENT)
    err = 0;
  return err;
}

int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int m_argc, const char *m_argv[]) {
  int err = 0;
  if (m_argc < 2 || (m_argc > 1 && (!strcmp(m_argv[1], "-h") || !strcmp(m_argv[1], "--help")))) {
    err = 1;
    zsv_overwrite_usage();
    return err;
  }

  const unsigned char *filepath = (const unsigned char *)m_argv[1];

  size_t row = 0;
  size_t col = 0;
  const char *val = NULL;

  if (m_argc == 2)
    return show_all_overwrites(filepath);

  for (int i = 1; !err && i < m_argc; i++) {
    const char *opt = m_argv[i];
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

  // If a non-integer is passed, atoi returns 0, which will trigger this condition
  if (!row || !col || !val) {
    fprintf(stderr, "Error: expected <row> <col> and <value>\n");
    err = 1;
  }

  if (err)
    return err;

  struct zsv_opts opts = {0};
  struct zsv_overwrite_opts overwrite_opts = {0};

  const char *overwrite_fn =
    (const char *)zsv_cache_filepath((const unsigned char *)filepath, zsv_cache_type_overwrite, 0, 0);
  overwrite_opts.src = overwrite_fn;

  if (!(opts.overwrite.ctx = zsv_overwrite_context_new(&overwrite_opts))) {
    fprintf(stderr, "Out of memory!\n");
    err = 1;
  } else {
    opts.overwrite.open = zsv_overwrite_open;
    opts.overwrite.next = zsv_overwrite_next;
    opts.overwrite.close = zsv_overwrite_context_delete;
  }

  if (!err) {
    printf("Loaded file: %s\n", filepath);
  }

  return err;
}
