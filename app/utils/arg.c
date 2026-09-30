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
#ifndef NO_THREADING
#define ZSVTLS _Thread_local
#else
#define ZSVTLS
#endif
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
  ZSVTLS static struct zsv_opts zsv_default_opts = {0};

  switch (mode) {
  case 'c': // clear
    memset(&zsv_default_opts, 0, sizeof(zsv_default_opts));
    zsv_default_opts_initd = 0;
    break;
  case 'g': // get
    if (!zsv_default_opts_initd) {
      zsv_default_opts_initd = 1;
      zsv_default_opts.max_row_size = ZSV_ROW_MAX_SIZE_DEFAULT;
      zsv_default_opts.max_columns = ZSV_MAX_COLS_DEFAULT;
    } else {
      zsv_default_opts.max_row_size =
        zsv_default_opts.max_row_size ? zsv_default_opts.max_row_size : ZSV_ROW_MAX_SIZE_DEFAULT;
      zsv_default_opts.max_columns = zsv_default_opts.max_columns ? zsv_default_opts.max_columns : ZSV_MAX_COLS_DEFAULT;
    }
    break;
  }
  return &zsv_default_opts;
}

ZSV_EXPORT
void zsv_clear_default_opts(void) {
  zsv_with_default_opts('c');
}

ZSV_EXPORT
struct zsv_opts zsv_get_default_opts(void) {
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
  for (i = 0; list[i] && strcmp(list[i], s); i++)
    ;
  return i;
}

#ifdef ZSV_EXTRAS

ZSV_EXPORT
void zsv_set_default_progress_callback(zsv_progress_callback cb, void *ctx, size_t rows_interval,
                                       unsigned int seconds_interval) {
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
enum zsv_arg_match zsv_arg_to_opts(int argc, const char *argv[], int *arg_i, struct zsv_opts *opts) {
#ifdef ZSV_EXTRAS
  static const char *short_args = "BcrtOqvRdSu01L";
#else
  static const char *short_args = "BcrtOqvRdSu0";
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
    "malformed-utf8-replacement",
    "header-row",
#ifdef ZSV_EXTRAS
    "apply-overwrites",
    "limit-rows",
#endif
    NULL,
  };

  int i = *arg_i;
  if (i < 0 || i >= argc || argv[i][0] != '-' || !argv[i][1]) /* an operand, or "-" (stdin) */
    return zsv_arg_none;

  int err = 0;
  char arg = 0;
  if (argv[i][1] != '-') {
    char *strchr_result;
    if (!argv[i][2] && (strchr_result = strchr(short_args, argv[i][1])))
      arg = *strchr_result;
#ifndef ZSV_NO_ONLY_CRLF
  } else if (!strcmp(argv[i] + 2, "only-crlf")) {
    opts->only_crlf_rowend = 1;
    return zsv_arg_ok;
#endif
  } else if (!strcmp(argv[i] + 2, "stdin-filename")) {
    if (++i >= argc || !*argv[i])
      err = fprintf(stderr, "Error: --stdin-filename requires a non-empty value\n");
    else if (!strcmp(argv[i], "-"))
      err = fprintf(stderr, "Error: --stdin-filename value may not be '-'\n");
    else
      opts->stdin_filename = argv[i];
    *arg_i = i < argc ? i : argc - 1;
    return err ? zsv_arg_err : zsv_arg_ok;
  } else if (!strcmp(argv[i] + 2, "parser")) {
    if (++i >= argc)
      err = fprintf(stderr, "Error: --parser requires a value (default, fast, or compat)\n");
    else if (!strcmp(argv[i], "default"))
      opts->scan_engine = 0; /* use compiled default */
    else if (!strcmp(argv[i], "fast"))
      opts->scan_engine = 3; /* ZSV_MODE_DELIM_FAST */
    else if (!strcmp(argv[i], "compat"))
      opts->scan_engine = 255; /* force compat/standard engine */
    else
      err = fprintf(stderr, "Error: --parser value must be 'default', 'fast', or 'compat' (got '%s')\n", argv[i]);
    *arg_i = i < argc ? i : argc - 1;
    return err ? zsv_arg_err : zsv_arg_ok;
  } else
    arg = short_args[str_array_index_of(long_args, argv[i] + 2)];

  switch (arg) {
  case 't':
    opts->delimiter = '\t';
    break;
  case 'S':
    opts->keep_empty_header_rows = 1;
    break;
  case 'q':
    opts->no_quotes = 1;
    break;
  case 'v':
    opts->verbose = 1;
    break;
#ifdef ZSV_EXTRAS
  case '1':
    opts->overwrite_auto = 1;
    break;
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
    if (++i >= argc) {
      err = fprintf(stderr, "Error: option %s requires a value\n", argv[i - 1]);
      i = argc - 1;
    } else {
      const char *val = argv[i];
      if (arg == 'O') {
        if (strlen(val) != 1 || *val == 0)
          err = fprintf(stderr, "Error: delimiter '%s' may only be a single ascii character", val);
        else if (strchr("\n\r\"", *val))
          err = fprintf(stderr, "Error: column delimiter may not be '\\n', '\\r' or '\"'\n");
        else
          opts->delimiter = *val;
      } else if (arg == 'u') {
        if (!strcmp(val, "none"))
          opts->malformed_utf8_replace = ZSV_MALFORMED_UTF8_DO_NOT_REPLACE;
        else if (!*val)
          opts->malformed_utf8_replace = ZSV_MALFORMED_UTF8_REMOVE;
        else if (strlen(val) > 2 || *val < 0)
          err =
            fprintf(stderr, "Error: %s value must be a single-byte UTF8 char, empty string or 'none'\n", argv[i - 1]);
        else
          opts->malformed_utf8_replace = *val;
      } else if (arg == '0') {
        if (*val == 0)
          err = fprintf(stderr, "Invalid empty Inserted header row\n");
        else
          opts->insert_header_row = argv[i];
      } else {
        /* arg = 'B', 'c', 'r', 'R', 'd', or 'L' (ZSV_EXTRAS only) */
        long n = atol(val);
        if (n < 0)
          err = fprintf(stderr, "Error: option %s value may not be less than zero (got %li\n", val, n);
#ifdef ZSV_EXTRAS
        else if (arg == 'L') {
          if (n < 1)
            err = fprintf(stderr, "Error: max rows may not be less than 1 (got %s)\n", val);
          else
            opts->max_rows = n;
        } else
#endif
          if (arg == 'B') {
          if (n < ZSV_MIN_SCANNER_BUFFSIZE)
            err = fprintf(stderr, "Error: buff size may not be less than %u (got %s)\n", ZSV_MIN_SCANNER_BUFFSIZE, val);
          else
            opts->buffsize = n;
        } else if (arg == 'c') {
          if (n < 8)
            err = fprintf(stderr, "Error: max column count may not be less than 8 (got %s)\n", val);
          else
            opts->max_columns = n;
        } else if (arg == 'r') {
          if (n < ZSV_ROW_MAX_SIZE_MIN)
            err =
              fprintf(stderr, "Error: max row size size may not be less than %u (got %s)\n", ZSV_ROW_MAX_SIZE_MIN, val);
          else
            opts->max_row_size = n;
        } else if (arg == 'd') {
          if (n < 8 && n >= 0)
            opts->header_span = n;
          else
            err = fprintf(stderr, "Error: header_span must be an integer between 0 and 8\n");
        } else if (arg == 'R') {
          if (n >= 0)
            opts->rows_to_ignore = n;
          else
            err = fprintf(stderr, "Error: rows_to_skip must be >= 0\n");
        }
      }
    }
    break;
  default: /* not a zsv option */
    return zsv_arg_none;
  }

  if (arg == 'R')
    opts->option_overrides.skip_head = 1;
  else if (arg == 'd')
    opts->option_overrides.header_row_span = 1;
  else if (arg == 'c')
    opts->option_overrides.max_column_count = 1;
  else if (arg == 'u')
    opts->option_overrides.malformed_utf8_replacement = 1;
  *arg_i = i;
  return err ? zsv_arg_err : zsv_arg_ok;
}

ZSV_EXPORT
enum zsv_status zsv_args_to_opts(int argc, const char *argv[], int *argc_out, const char **argv_out,
                                 struct zsv_opts *opts_out) {
  *opts_out = zsv_get_default_opts();
  int new_argc = 0;
  if (argc > 0)
    argv_out[new_argc++] = argv[0];

  enum zsv_arg_match m = zsv_arg_none;
  for (int i = 1; m != zsv_arg_err && i < argc; i++) {
    if ((m = zsv_arg_to_opts(argc, argv, &i, opts_out)) == zsv_arg_none)
      argv_out[new_argc++] = argv[i];
  }

  *argc_out = new_argc;
  return m == zsv_arg_err ? zsv_status_error : zsv_status_ok;
}

const char *zsv_next_arg(int arg_i, int argc, const char *argv[], int *err) {
  if (!(arg_i < argc && strlen(argv[arg_i]) > 0)) {
    fprintf(stderr, "%s option value invalid: should be non-empty string\n", argv[arg_i - 1]);
    *err = 1;
    return NULL;
  }
  return argv[arg_i];
}

int zsv_arg_is_option(const char *arg) {
  return arg && arg[0] == '-' && arg[1] != '\0';
}

int zsv_err_unrecognized_option(const char *arg) {
  fprintf(stderr, "Unrecognized option: %s\n", arg);
  return 1;
}
