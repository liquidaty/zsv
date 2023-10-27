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
#include <zsv/utils/writer.h>

#define ZSV_COMMAND paste
#include "zsv_command.h"

static int zsv_paste_usage() {
  static const char *usage =
    "Usage: paste <filename> [<filename> ...]\n"
    "\n"
    "Options:\n"
    " -h, --help            : show usage\n";
  printf("%s\n", usage);
  return 1;
}

struct zsv_paste_input_file {
  struct zsv_paste_input_file *next;
  const char *fname;
  struct zsv_opts opts;
  FILE *f;
  unsigned col_count;
  zsv_parser parser;
  enum zsv_status zsv_status; // parser status
};

enum zsv_paste_status {
  zsv_paste_status_ok = 0,
  zsv_paste_status_file,
  zsv_paste_status_memory,
  zsv_paste_status_error
};

// zsv_paste_load_row: return number of inputs that a row was retrieved from
static int zsv_paste_load_row(struct zsv_paste_input_file *inputs) {
  int have_row = 0;
  for(struct zsv_paste_input_file *pf = inputs; pf; pf = pf->next) {
    if(pf->zsv_status == zsv_status_row) {
      pf->zsv_status = zsv_next_row(pf->parser);
      if(pf->zsv_status == zsv_status_row)
        have_row++;
    }
  }
  return have_row;
}

static void zsv_paste_print_row(zsv_csv_writer w, struct zsv_paste_input_file *inputs) {
  char first = 1;
  for(struct zsv_paste_input_file *pf = inputs; pf; pf = pf->next) {
    unsigned int j = pf->zsv_status == zsv_status_row ? zsv_cell_count(pf->parser) : 0;
    unsigned int k = pf->col_count;

    for(unsigned int i = 0; i < j && i < k; i++) {
      struct zsv_cell cell = zsv_get_cell(pf->parser, i);
      zsv_writer_cell(w, first, cell.str, cell.len, cell.quoted);
      first = 0;
    }
    for(unsigned int i = j; i < k; i++) {
      zsv_writer_cell(w, first, (const unsigned char *)"", 0, 0);
      first = 0;
    }
  }
}

static void zsv_paste_delete_input(struct zsv_paste_input_file *pf) {
  if(pf->parser)
    zsv_delete(pf->parser);
  if(pf->opts.stream)
    fclose(pf->opts.stream);
  free(pf);
}

// zsv_paste_add_input(): return error
static enum zsv_paste_status zsv_paste_add_input(
                                                 const char *fname,
                                                 struct zsv_paste_input_file **next,
                                                 struct zsv_paste_input_file ***next_next,
                                                 struct zsv_opts *opts,
                                                 struct zsv_prop_handler *custom_prop_handler,
                                                 const char *opts_used
                                                 ) {
  FILE *f = fopen(fname, "rb");
  if(!f) {
    perror(fname);
    return zsv_paste_status_file;
  }
  struct zsv_paste_input_file *pf = calloc(1, sizeof(*pf));
  if(!pf) {
    fclose(f);
    return zsv_paste_status_memory;
  }

  pf->opts = *opts;
  pf->opts.stream = f;
  pf->fname = fname;
  *next = pf;
  *next_next = &pf->next;

  if(zsv_new_with_properties(&pf->opts, custom_prop_handler, fname, opts_used, &pf->parser) != zsv_status_ok) {
    fprintf(stderr, "Unable to initialize parser for %s\n", fname);
    return zsv_paste_status_error;
  } else {
    if((pf->zsv_status = zsv_next_row(pf->parser)) == zsv_status_row)
      pf->col_count = zsv_cell_count(pf->parser);
    else
      fprintf(stderr, "Warning: no data read from %s\n", fname);
  }
  return zsv_paste_status_ok;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  struct zsv_paste_input_file *inputs = NULL;
  struct zsv_paste_input_file **next_input = &inputs;
  enum zsv_paste_status status = zsv_paste_status_ok;

  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  zsv_csv_writer writer = zsv_writer_new(&writer_opts);
  if(!writer) {
    status = zsv_printerr(zsv_paste_status_error, "Unable to create csv writer");
    goto zsv_paste_done;
  }

  for(int i = 1; status == zsv_paste_status_ok && i < argc; i++) {
    const char *arg = argv[i];
    if(!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      zsv_paste_usage();
      goto zsv_paste_done;
    }

    if(0) { // !strcmp(arg, "-x") || !strcmp(arg, "--my-arg")) { ...
    } else {
      status = zsv_paste_add_input(arg, next_input, &next_input, opts, custom_prop_handler, opts_used);
    }
  }

  if(status == zsv_paste_status_ok) {
    if(!inputs) {
      fprintf(stderr, "Please specify at least one input file\n");
      status = zsv_paste_status_error;
      goto zsv_paste_done;
    }

    // print headers
    zsv_paste_print_row(writer, inputs);

    // print one row at a time
    while(zsv_paste_load_row(inputs))
      zsv_paste_print_row(writer, inputs);
  }

 zsv_paste_done:
  for(struct zsv_paste_input_file *next, *pf = inputs; pf; pf = next) {
    next = pf->next;
    zsv_paste_delete_input(pf);
  }

  if(opts->stream && opts->stream != stdin)
    fclose(opts->stream);

  zsv_writer_delete(writer);
  return status;
}
