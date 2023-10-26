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
#include <assert.h>

/*
 * for now we don't really need thread support because this is only being used
 * by the CLI. However, it's here anyway in case future enhancements or
 * user customizations need multithreading support
 */
#ifndef ZSVTLS
# ifndef NO_THREADING
#  define ZSVTLS _Thread_local
# else
#  define ZSVTLS
# endif
#endif
/*
 * global zsv_default_opts for convenience funcs zsv_get_default_opts() and zsv_set_default_opts()
 *  for the cli to pass global opts to the standalone modules
 */

/*
 * Use a single function for all default-option operations, so as to be able
 * to use thread-local storage with static initializer
 */
static struct zsv_opts *zsv_with_default_opts(char mode) {
  ZSVTLS static char zsv_default_opts_initd = 0;
  ZSVTLS static struct zsv_opts zsv_default_opts = { 0 };

  switch(mode) {
  case 'c': // clear
    memset(&zsv_default_opts, 0, sizeof(zsv_default_opts));
    zsv_default_opts_initd = 0;
    break;
  case 'g': // get
    if(!zsv_default_opts_initd) {
      zsv_default_opts_initd = 1;
      zsv_default_opts.max_row_size = ZSV_ROW_MAX_SIZE_DEFAULT;
      zsv_default_opts.max_columns = ZSV_MAX_COLS_DEFAULT;
    } else {
      zsv_default_opts.max_row_size = zsv_default_opts.max_row_size ? zsv_default_opts.max_row_size : ZSV_ROW_MAX_SIZE_DEFAULT;
      zsv_default_opts.max_columns = zsv_default_opts.max_columns ? zsv_default_opts.max_columns : ZSV_MAX_COLS_DEFAULT;
    }
    break;
  }
  return &zsv_default_opts;
}

ZSV_EXPORT
void zsv_clear_default_opts() {
  zsv_with_default_opts('c');
}

ZSV_EXPORT
struct zsv_opts zsv_get_default_opts() {
  return *zsv_with_default_opts('g');
}

ZSV_EXPORT
void zsv_set_default_opts(struct zsv_opts opts) {
  *zsv_with_default_opts(0) = opts;
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
/**
 * Convert common command-line arguments to zsv_opts
 * Return new argc/argv values with processed args stripped out
 * Initializes opts_out with `zsv_get_default_opts()`, then with
 * the below common options if present:
 *     -B,--buff-size <N>
 *     -c,--max-column-count <N>
 *     -r,--max-row-size <N>
 *     -t,--tab-delim
 *     -O,--other-delim <C>
 *     -q,--no-quote
 *     -R,--skip-head <n>: skip specified number of initial rows
 *     -d,--header-row-span <n> : apply header depth (rowspan) of n
 *     -u,--malformed-utf8-replacement <replacement_string>: replacement string (can be empty) in case of malformed UTF8 input
 *       (default for "desc" commamnd is '?')
 *     -S,--keep-blank-headers  : disable default behavior of ignoring leading blank rows
 *     -0,--header-row <header> : insert the provided CSV as the first row (in position 0)
 *                                e.g. --header-row 'col1,col2,\"my col 3\"'",
 *     -v,--verbose
 *
 * @param  argc      count of args to process
 * @param  argv      args to process
 * @param  argc_out  count of unprocessed args
 * @param  argv_out  array of unprocessed arg values. Must be allocated by caller
 *                   with size of at least argc * sizeof(*argv)
 * @param  opts_out  options, updated to reflect any processed args
 * @param  opts_used optional; if provided:
 *                   - must point to >= ZSV_OPTS_SIZE_MAX bytes of storage
 *                   - all used options will be returned in this string
 *                   e.g. if -R and -q are used, then opts_used will be set to:
 *                     "     q R   "
 * @return           zero on success, non-zero on error
 */
ZSV_EXPORT
enum zsv_status zsv_args_to_opts(int argc, const char *argv[],
                                 int *argc_out, const char **argv_out,
                                 struct zsv_opts *opts_out,
                                 char *opts_used
                                 ) {
#ifdef ZSV_EXTRAS
  static const char *short_args = "BcrtOqvRdSu0L";
#else
  static const char *short_args = "BcrtOqvRdSu0";
#endif
  assert(strlen(short_args) < ZSV_OPTS_SIZE_MAX);

  static const char *long_args[] = { //
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
    "malformed-utf8-replacement",
    "header-row",
#ifdef ZSV_EXTRAS
    "limit-rows",
#endif
    NULL
  };

  *opts_out = zsv_get_default_opts();
  int options_start = 1; // skip this many args before we start looking for options
  int err = 0;
  int new_argc = 0;
  for(; new_argc < options_start && new_argc < argc; new_argc++)
    argv_out[new_argc] = argv[new_argc];
  if(opts_used) {
    memset(opts_used, ' ', ZSV_OPTS_SIZE_MAX-1);
    opts_used[ZSV_OPTS_SIZE_MAX-1] = '\0';
  }

  for(int i = options_start; !err && i < argc; i++) {
    char arg = 0;
    if(*argv[i] != '-') { /* pass this option through */
      argv_out[new_argc++] = argv[i];
      continue;
    }
    unsigned found_ix = 0;
    if(argv[i][1] != '-') {
      char *strchr_result;
      if(!argv[i][2] && (strchr_result = strchr(short_args, argv[i][1]))) {
        arg = argv[i][1];
        found_ix = strchr_result - short_args;
      }
    } else {
      found_ix = str_array_index_of(long_args, argv[i] + 2);
      arg = short_args[found_ix];
    }

    char processed = 1;
    switch(arg) {
    case 't':
      opts_out->delimiter = '\t';
      break;
    case 'S':
      opts_out->keep_empty_header_rows = 1;
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
    case 'u':
    case '0':
      if(++i >= argc)
        err = fprintf(stderr, "Error: option %s requires a value\n", argv[i-1]);
      else {
        const char *val = argv[i];
        if(arg == 'O') {
          if(strlen(val) != 1 || *val == 0)
            err = fprintf(stderr, "Error: delimiter '%s' may only be a single ascii character", val);
          else if(strchr("\n\r\"", *val))
            err = fprintf(stderr, "Error: column delimiter may not be '\\n', '\\r' or '\"'\n");
        else
          opts_out->delimiter = *val;
        } else if(arg == 'u') {
          if(!strcmp(val, "none"))
            opts_out->malformed_utf8_replace = ZSV_MALFORMED_UTF8_DO_NOT_REPLACE;
          else if(!*val)
            opts_out->malformed_utf8_replace = ZSV_MALFORMED_UTF8_REMOVE;
          else if(strlen(val) > 2 || *val < 0)
            err = fprintf(stderr, "Error: %s value must be a single-byte UTF8 char, empty string or 'none'\n", argv[i-1]);
          else
            opts_out->malformed_utf8_replace = *val;
        } else if(arg == '0') {
          if(*val == 0)
            err = fprintf(stderr, "Invalid empty Inserted header row\n");
          else
            opts_out->insert_header_row = argv[i];
        } else {
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
                opts_out->rows_to_ignore = n;
              else
                err = fprintf(stderr, "Error: rows_to_skip must be >= 0\n");
            }
        }
      }
      break;
    default: /* pass this option through */
      processed = 0;
      argv_out[new_argc++] = argv[i];
      break;
    }
    if(processed && opts_used)
      opts_used[found_ix] = arg;
  }

  *argc_out = new_argc;
  return err ? zsv_status_error : zsv_status_ok;
}

const char *zsv_next_arg(int arg_i, int argc, const char *argv[], int *err) {
  if(!(arg_i < argc && strlen(argv[arg_i]) > 0)) {
    fprintf(stderr, "%s option value invalid: should be non-empty string\n", argv[arg_i-1]);
    *err = 1;
    return NULL;
  }
  return argv[arg_i];
}
