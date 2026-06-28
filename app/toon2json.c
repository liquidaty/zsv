/*
 * Copyright (C) 2021-2022 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ZSV_COMMAND toon2json
#include "zsv_command.h"

#include <zsv/utils/toon.h>

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  (void)optsp;
  (void)custom_prop_handler;

  const char *usage[] = {
    ZSV_USAGE_PROG " " APPNAME ": convert TOON to JSON (the inverse of " ZSV_USAGE_PROG " 2toon)",
    "",
    "Usage: " ZSV_USAGE_PROG " " APPNAME " [options] [file.toon]",
    "",
    "Options:",
    "  -h,--help              : show usage",
    "  -o,--output <filename> : write output to file instead of stdout",
    "  --compact              : accepted for symmetry; JSON output is always compact",
    "",
    "If no input file is given, TOON is read from stdin.",
    "See `" ZSV_USAGE_PROG " help toon` for the TOON encoding grammar.",
    NULL,
  };

  FILE *in = NULL;
  FILE *out = NULL;
  const char *input_path = NULL;
  int compact = 0;
  int err = 0;
  int done = 0;

  for (int i = 1; !err && !done && i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      zsv_print_usage(usage);
      done = 1;
    } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if (++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i - 1]), err = 1;
      else if (out)
        fprintf(stderr, "Output file specified more than once\n"), err = 1;
      else if (!(out = fopen(argv[i], "wb")))
        fprintf(stderr, "Unable to open for writing: %s\n", argv[i]), err = 1;
    } else if (!strcmp(argv[i], "--compact")) {
      compact = 1;
    } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]), err = 1;
    } else {
      if (in)
        fprintf(stderr, "Input file specified more than once\n"), err = 1;
      else if (!(in = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = 1;
      else
        input_path = argv[i];
    }
  }
  (void)input_path;

  if (!err && !done) {
    if (!in) {
#ifdef NO_STDIN
      fprintf(stderr, "Please specify an input file\n");
      err = 1;
#else
      in = stdin;
#endif
    }
    if (!out)
      out = stdout;
    if (!err)
      err = zsv_toon_to_json(in, out, compact);
  }

  if (in && in != stdin)
    fclose(in);
  if (out && out != stdout)
    fclose(out);
  return err;
}
