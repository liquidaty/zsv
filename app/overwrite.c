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

#define ZSV_COMMAND overwrite 
#include "zsv_command.h"

#include <zsv/utils/compiler.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/string.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/overwrite.h>

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
  "is kept in the \"overwrites\" table of /path/to/.zsv/data/my-data.csv/overwrite.sqlite3."
};

static int zsv_overwrite_usage() {
  for (size_t i = 0; zsv_overwrite_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_overwrite_usage_msg[i]);
  return 1;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (argc < 1 || (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))) {
    zsv_overwrite_usage();
    return 0;
  }
  int err = 0;
  return err;
}
