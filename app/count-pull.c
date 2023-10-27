/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ZSV_COMMAND count_pull
#include "zsv_command.h"

static int count_usage() {
  static const char *usage =
    "Usage: count [options]\n"
    "Options:\n"
    " -h, --help            : show usage\n"
    " [-i, --input] <filename>: use specified file input\n";
  printf("%s\n", usage);
  return 0;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  const char *input_path = NULL;
  int err = 0;
  for(int i = 1; !err && i < argc; i++) {
    const char *arg = argv[i];
    if(!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      count_usage();
      goto count_done;
    } if(!strcmp(arg, "-i") || !strcmp(arg, "--input") || *arg != '-') {
      err = 1;
      if((!strcmp(arg, "-i") || !strcmp(arg, "--input")) && ++i >= argc)
        fprintf(stderr, "%s option requires a filename\n", arg);
      else {
        if(opts->stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        else if(!(opts->stream = fopen(argv[i], "rb")))
          fprintf(stderr, "Unable to open for reading: %s\n", argv[i]);
        else {
          input_path = argv[i];
          err = 0;
        }
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

#ifdef NO_STDIN
  if(!opts->stream || opts->stream == stdin) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  if(!err) {
    zsv_parser parser;
//    if(zsv_pull_new_with_properties(opts, input_path, opts_used, &parser) != zsv_status_ok) {
    if(zsv_new_with_properties(opts, custom_prop_handler, input_path, opts_used, &parser) != zsv_status_ok) {
      fprintf(stderr, "Unable to initialize parser\n");
      err = 1;
    } else {
      size_t count = 0;
//      zsv_pull_row r;
      while(zsv_next_row(parser) == zsv_status_row)
        count++;
      zsv_delete(parser);
      printf("%zu\n", count  > 0 ? count - 1 : 0);
    }
  }

 count_done:
  if(opts->stream && opts->stream != stdin)
    fclose(opts->stream);

  return err;
}
