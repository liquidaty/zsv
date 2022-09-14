/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/signal.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/prop.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct data {
  zsv_parser parser;
  size_t rows;
};

static void row(void *ctx) {
  ((struct data *)ctx)->rows++;
}

#ifndef MAIN
#define MAIN main
#endif

static int count_usage() {
  static const char *usage =
    "Usage: count [options]\n"
    "Options:\n"
    " -h, --help            : show usage\n"
    " [-i, --input] <filename>: use specified file input\n";
  printf("%s\n", usage);
  return 0;
}

int MAIN(int argc, const char *argv[]) {
  struct data data = { 0 };
  const char *input_path = NULL;
  struct zsv_opts opts; // = zsv_get_default_opts();
  char opts_used[ZSV_OPTS_SIZE_MAX];
  enum zsv_status stat = zsv_args_to_opts(argc, argv, &argc, argv, &opts, opts_used);
  if(stat != zsv_status_ok)
    return stat;


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
        if(opts.stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        else if(!(opts.stream = fopen(argv[i], "rb")))
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
  if(!opts.stream || opts.stream == stdin) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  if(!err) {
    opts.row = row;
    opts.ctx = &data;
    if(zsv_new_with_properties(&opts, input_path, opts_used, &data.parser) != zsv_status_ok) {
      fprintf(stderr, "Unable to initialize parser\n");
      err = 1;
    } else {
      enum zsv_status status;
      while((status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;
      zsv_finish(data.parser);
      zsv_delete(data.parser);
      printf("%zu\n", data.rows  > 0 ? data.rows - 1 : 0);
    }
  }

 count_done:
  if(opts.stream && opts.stream != stdin)
    fclose(opts.stream);

  return err;
}
