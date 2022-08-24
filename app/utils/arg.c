/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zsv.h>
#include <zsv/utils/string.h>
#include <zsv/utils/arg.h>

/*
 * global zsv_default_opts for convenience funcs zsv_get_default_opts() and zsv_set_default_opts()
 *  for the cli to pass global opts to the standalone modules
 */
struct zsv_opts zsv_default_opts = { 0 };
char zsv_default_opts_initd = 0;

ZSV_EXPORT
void zsv_clear_default_opts() {
  memset(&zsv_default_opts, 0, sizeof(zsv_default_opts));
  zsv_default_opts_initd = 0;
}

ZSV_EXPORT
struct zsv_opts zsv_get_default_opts() {
  if(!zsv_default_opts_initd) {
    zsv_default_opts_initd = 1;
    zsv_default_opts.max_row_size = ZSV_ROW_MAX_SIZE_DEFAULT;
    zsv_default_opts.max_columns = ZSV_MAX_COLS_DEFAULT;
  }
  return zsv_default_opts;
}

ZSV_EXPORT
void zsv_set_default_opts(struct zsv_opts opts) {
  zsv_default_opts = opts;
}

/**
 * str_array_index_of: return index in list, or size of list if not found
 */
static inline int str_array_index_of(const char *list[], const char *s) {
  int i;
  for(i = 0; list[i] && strcmp(list[i], s); i++) ;
  return i;
}

#ifdef ZSV_EXTRAS

ZSV_EXPORT
void zsv_set_default_progress_callback(zsv_progress_callback cb, void *ctx, size_t rows_interval, unsigned int seconds_interval) {
  struct zsv_opts opts = zsv_get_default_opts();
  opts.progress.callback = cb;
  opts.progress.ctx = ctx;
  opts.progress.rows_interval = rows_interval;
  opts.progress.seconds_interval = seconds_interval;
  zsv_set_default_opts(opts);
}

ZSV_EXPORT
void zsv_set_default_completed_callback(zsv_completed_callback cb, void *ctx) {
  struct zsv_opts opts = zsv_get_default_opts();
  opts.completed.callback = cb;
  opts.completed.ctx = ctx;
  zsv_set_default_opts(opts);
}

#endif

ZSV_EXPORT
int zsv_args_to_opts(int argc, const char *argv[],
                     int *argc_out, const char **argv_out,
                     struct zsv_opts *opts_out
                     ) {
  *opts_out = zsv_get_default_opts();
  int options_start = 1; // skip this many args before we start looking for options
  int err = 0;
  int new_argc = 0;
  for(; new_argc < options_start && new_argc < argc; new_argc++)
    argv_out[new_argc] = argv[new_argc];

#ifdef ZSV_EXTRAS
  static const char *short_args = "BcrtOqvRdSL";
#else
  static const char *short_args = "BcrtOqvRdS";
#endif

  static const char *long_args[] = {
    "buff-size",
    "max-column-count",
    "max-row-size",
    "tab-delim",
    "other-delim",
    "no-quote",
    "verbose",
    "skip-head",
    "header-row-span",
    "keep-blank-headers",
#ifdef ZSV_EXTRAS
    "limit-rows",
#endif
    NULL
  };
  for(int i = options_start; !err && i < argc; i++) {
    char arg = 0;
    if(*argv[i] != '-') { /* pass this option through */
      argv_out[new_argc++] = argv[i];
      continue;
    }
    if(argv[i][1] != '-') {
      if(!argv[i][2] && strchr(short_args, argv[i][1]))
        arg = argv[i][1];
    } else
      arg = short_args[str_array_index_of(long_args, argv[i] + 2)];

    switch(arg) {
    case 't':
      opts_out->delimiter = '\t';
      break;
    case 'S':
      opts_out->no_skip_empty_header_rows = 1;
      break;
    case 'q':
      opts_out->no_quotes = 1;
      break;
    case 'v':
      opts_out->verbose = 1;
      break;
#ifdef ZSV_EXTRAS
    case 'L':
#endif
    case 'B':
    case 'c':
    case 'r':
    case 'O':
    case 'R':
    case 'd':
      if(++i >= argc)
        err = fprintf(stderr, "Error: option %s requires a value\n", argv[i-1]);
      else if(arg == 'O') {
        const char *val = argv[i];
        if(strlen(val) != 1 || *val == 0)
          err = fprintf(stderr, "Error: delimiter '%s' may only be a single ascii character", val);
        else if(strchr("\n\r\"", *val))
          err = fprintf(stderr, "Error: column delimiter may not be '\\n', '\\r' or '\"'\n");
        else
          opts_out->delimiter = *val;
      } else {
        const char *val = argv[i];
        /* arg = 'B', 'c', 'r', 'R', 'd', or 'L' (ZSV_EXTRAS only) */
        long n = atol(val);
        if(n < 0)
          err = fprintf(stderr, "Error: option %s value may not be less than zero (got %li\n", val, n);
#ifdef ZSV_EXTRAS
        else if(arg == 'L') {
          if(n < 1)
            err = fprintf(stderr, "Error: max rows may not be less than 1 (got %s)\n", val);
          else
            opts_out->max_rows = n;
        } else
#endif
        if(arg == 'B') {
          if(n < ZSV_MIN_SCANNER_BUFFSIZE)
            err = fprintf(stderr, "Error: buff size may not be less than %u (got %s)\n",
                          ZSV_MIN_SCANNER_BUFFSIZE, val);
          else
            opts_out->buffsize = n;
        } else if(arg == 'c') {
          if(n < 8)
            err = fprintf(stderr, "Error: max column count may not be less than 8 (got %s)\n", val);
          else
            opts_out->max_columns = n;
        } else if(arg == 'r') {
          if(n < ZSV_ROW_MAX_SIZE_MIN)
            err = fprintf(stderr, "Error: max row size size may not be less than %u (got %s)\n",
                          ZSV_ROW_MAX_SIZE_MIN, val);
          else
            opts_out->max_row_size = n;
        } else if(arg == 'd') {
          if(n < 8 && n >= 0)
            opts_out->header_span = n;
          else
            err = fprintf(stderr, "Error: header_span must be an integer between 0 and 8\n");
        } else if(arg == 'R') {
          if(n >= 0)
            opts_out->rows_to_skip = n;
          else
            err = fprintf(stderr, "Error: rows_to_skip must be >= 0\n");
        }
      }
      break;
    default: /* pass this option through */
      argv_out[new_argc++] = argv[i];
      break;
    }
  }

  *argc_out = new_argc;
  return err;
}
