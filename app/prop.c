/* Copyright (C) 2022 Guarnerix Inc dba Liquidaty - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Matt Wong <matt@guarnerix.com>
 */

/*
  for a given file, edits properties saved in ZSV_CACHE_DIR/<filepath>/props.json
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h> // unlink, access

#define ZSV_COMMAND_NO_OPTIONS
#define ZSV_COMMAND prop
#include "zsv_command.h"

#include <zsv/utils/os.h>
#include <zsv/utils/file.h>
#include <zsv/utils/json.h>
#include <zsv/utils/jq.h>
#include <zsv/utils/dirs.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/string.h>

const char *zsv_property_usage_msg[] = {
  APPNAME ": view or save parsing options associated with a file",
  "          saved options will be applied by default when processing that file",
  "",
  "Usage: " APPNAME " <filepath> [options]",
  "  where filepath is the path to the input CSV file, or",
  "    when using --auto: input CSV file or - for stdin",
  "    when using --clean: directory to clean from (use '.' for current directory)",
  "  and options may be one or more of:",
  "    -d,--header-row-span <value>: set/unset/auto-detect header depth (see below)",
  "    -R,--skip-head <value>      : set/unset/auto-detect initial rows to skip (see below)",
  "    --list-files                : list all property sets associted with the given file", // output a list of all cache files
  "    --clear                     : delete all properties of the specified file",
  // TO DO: --clear-file relative-path
  "    --clean                     : delete all files / dirs in the property cache of the given directory",
  "                                  that do not have a corresponding file in that directory",
  "      --dry                     : dry run, outputs files/dirs to remove. only for use with --clean",
  "    --auto                      : guess the best property values. This is equivalent to:",
  "                                    -d auto -R auto",
  "                                  when using this option, a dash (-) can be used instead",
  "                                  of a filepath to read from stdin",
  "    --save [-f,--overwrite]     : (only applicable with --auto) save the detected result",
  "    --copy <dest filepath>      : copy properties to another file", // to do: opt to check valid JSON
  "    --export <output path>      : export all properties to a single JSON file (- for stdout)", // to do: opt to check valid JSON
  "    --import <input path>       : import properties from a single JSON file (- for stdin)", // to do: opt to check valid JSON
  "    -f,--overwrite              : overwrite any previously-saved properties",
  "",
  "For --header-row-span or --skip-head options, <value> can be:",
  "  - a positive integer, to save the value to the associated file's properties",
  "  - a zero (0), or \"none\" or \"-\" to remove the value from the associated",
  "    file's properties",
  "  - \"auto\" to auto-detect the property value (to save, use --save/--overwrite)",
  "",
  "If no options are provided, currently saved properties are output in JSON format.",
  "",
  "  Properties are saved in " ZSV_CACHE_DIR "/<filename>/" ZSV_CACHE_PROPERTIES_NAME,
  "    which is deleted when the file is removed using `rm`",
  "",
  "The --auto feature is provided for convenience only, and is not intended to be smart enough",
  "  to make guesses that can be blindly assumed to be correct. You have been warned!",
  NULL
};

static int zsv_property_usage(FILE *target) {
  for(int j = 0; zsv_property_usage_msg[j]; j++)
    fprintf(target, "%s\n", zsv_property_usage_msg[j]);
  return target == stdout ? 0 : 1;
}

static int show_all_properties(const unsigned char *filepath) {
  int err = 0;
  // to do: show all files, not just prop file

  if(!zsv_file_readable((const char *)filepath, &err, NULL)) {
    perror((const char *)filepath);
    return err;
  }
  err = zsv_cache_print(filepath, zsv_cache_type_property, (const unsigned char *)"{}");
  if(err == ENOENT)
    err = 0;
  return err;
}

#define ZSV_PROP_TYPE_CHECK_NUM 1
#define ZSV_PROP_TYPE_CHECK_DATE 2
#define ZSV_PROP_TYPE_CHECK_BOOL 4
#define ZSV_PROP_TYPE_CHECK_NULL 8

/**
 * Very basic test to check if a string looks like a number:
 * - ignore leading whitespace and currency
 * - ignore trailing whitespace
 * - ignore leading dash or plus
 * - len < 1 or > 30 => not a number
 * - scan characters one by one:
 *     if the char isn't a digit, comma or period, it's not a number
 *     digits are ignored
 *     commas are counted (we ignore the requirement for them to be spaced out e.g. every 3 digits)
 *     periods are counted
 *     if at any point we have more than 1 comma AND more than 1 digit, it's not a number
 * @param s     input string
 * @param len   length of input
 * @param flags reserved for future use
 * @return      1 if it looks like a number, else 0
 */
static char looks_like_num(const unsigned char *s, size_t len, unsigned flags) {
  (void)(flags);
  // trim
  s = zsv_strtrim(s, &len);

  // strip +/- sign, if any
  size_t sign = zsv_strnext_is_sign(s, len);
  if(sign) {
    s += sign;
    len -= sign;
    s = zsv_strtrim_left(s, &len);
  }

  // strip currency, if any
  size_t currency = zsv_strnext_is_currency(s, len);
  if(currency) {
    s += currency;
    len -= currency;
    s = zsv_strtrim_left(s, &len);
  }

  // strip +/- sign, if we didn't find one earlier
  if(!sign && (sign = zsv_strnext_is_sign(s, len))) {
    s += sign;
    len -= sign;
    s = zsv_strtrim_left(s, &len);
  }

  if(len < 1 || len > 30)
    return 0;

  unsigned digits = 0;
  unsigned period = 0;
  for(size_t i = 0; i < len; i++) {
    unsigned char c = s[i];
    if(c >= '0' && c <= '9') // to do: allow utf8 digits, commas, periods?
      digits++;
    else if(c == ',' && i > 0 && period == 0) { // comma can't be first char, or follow a period
      // do nothing. to do: check that the last comma was either 3 or 4 numbers away?
    } else if(c == '.' && period == 0) // only 1 period allowed (to do: relax this as it isn't true in all localities)
      period++;
    else
      return 0;
  }
  return digits > 0 && period < 2;
}

/**
 * Super crude "test" to check if a string looks like a date or timestamp:
 * we are just going to disqualify if len < 5 or len > 30
 * or any chars are not digits, slash, dash, colon, space
 * or in any of the following which is made up of chars from the English months, plus am/pm
 *   abcdefghijlmnoprstuvy
 * @param s     input string
 * @param len   length of input
 * @param flags reserved for future use
 * @return      1 if it looks like a date, else 0
 */
static char looks_like_date(const unsigned char *s, size_t len, unsigned flags) {
  (void)(flags);
  // trim
  s = zsv_strtrim(s, &len);
  if(len <= 5 || len > 30)
    return 0;
  #define LOOKS_LIKE_DATE_CHARS "0123456789-/:, abcdefghijlmnoprstuvy"
  for(size_t i = 0; i < len; i++)
    if(!memchr(LOOKS_LIKE_DATE_CHARS, s[i], strlen(LOOKS_LIKE_DATE_CHARS)))
      return 0;
  return 1;
}

/**
 * Very basic test to check if a string looks like a bool:
 * - ignore leading and trailing whitespace
 * - look for true, false, yes, no, T, F, 1, 0, Y, N
 * - to do: add localization options?
 * @param s     input string
 * @param len   length of input
 * @param flags reserved for future use
 * @return      1 if it looks like a bool, else 0
 */
static char looks_like_bool(const unsigned char *s, size_t len, unsigned flags) {
  (void)(flags);
  // trim
  s = zsv_strtrim(s, &len);

  if(!len)
    return 0;

  if(len == 1)
    return strchr("TtFf10YyNn", *s) ? 1 : 0;

  if(len <= 5) {
    char *lower = (char *)zsv_strtolowercase(s, &len);
    if(lower) {
      char result = 0;
      switch(len) {
      case 2:
        result = !strcmp(lower, "no");
        break;
      case 3:
        result = !strcmp(lower, "yes");
        break;
      case 4:
        result = !strcmp(lower, "true");
        break;
      case 5:
        result = !strcmp(lower, "false");
        break;
      }
      free(lower);
      return result;
    }
  }
  return 0;
}

static unsigned int type_detect(const unsigned char *s, size_t slen) {
  unsigned int result = 0;
  if(slen == 0) {
    result += ZSV_PROP_TYPE_CHECK_NULL;
    return result;
  }
  if(looks_like_num(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_NUM;
  if(looks_like_date(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_DATE;
  if(looks_like_bool(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_BOOL;
  return result;
}

#define ZSV_PROP_DETECT_ROW_MAX 10
struct detect_properties_data {
  zsv_parser parser;
  struct {
    size_t date;
    size_t num;
    size_t is_bool;
    size_t null;
    size_t cols_used;
  } rows[ZSV_PROP_DETECT_ROW_MAX];
  size_t rows_processed;
};

static void detect_properties_row(void *ctx) {
  struct detect_properties_data *data = ctx;
  size_t cols_used = data->rows[data->rows_processed].cols_used = zsv_cell_count(data->parser);
  for(size_t i = 0; i < cols_used; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    unsigned int result = type_detect(c.str, c.len);
    if(result & ZSV_PROP_TYPE_CHECK_NULL)
      data->rows[data->rows_processed].null++;
    else {
      if(result & ZSV_PROP_TYPE_CHECK_NUM)
        data->rows[data->rows_processed].num++;
      if(result & ZSV_PROP_TYPE_CHECK_DATE)
        data->rows[data->rows_processed].date++;
      if(result & ZSV_PROP_TYPE_CHECK_BOOL)
        data->rows[data->rows_processed].is_bool++;
    }
  }
  if(++data->rows_processed >= ZSV_PROP_DETECT_ROW_MAX)
    zsv_abort(data->parser);
}

static struct zsv_file_properties guess_properties(struct detect_properties_data *data) {
  struct zsv_file_properties result = { 0 };
  size_t i;
  for(i = 0; i < data->rows_processed; i++) {
    if(data->rows[i].cols_used <= data->rows[i].null)
      result.skip++;
    else if(i + 1 < data->rows_processed && data->rows[i+1].cols_used <= data->rows[i+1].null)
      result.skip++;
    else
      break;
  }

  result.header_span = 1;
  for(i = i+1; i < data->rows_processed; i++, result.header_span++) {
    if(data->rows[i].cols_used == data->rows[i].null)
      continue;
    if(data->rows[i].cols_used > data->rows[i-1].cols_used)
      continue;
    unsigned int data_like = data->rows[i].date + data->rows[i].num + data->rows[i].is_bool;
    if(data_like > 5 || (data_like > 0 && data_like >= data->rows[i].cols_used / 2))
      break;
    if(data_like + data->rows[i].null == data->rows[i].cols_used)
      break;
    if(data_like <= 5 && data_like > 0 && data->rows[i].cols_used <= 5)
      break;
  }
  if(result.header_span == data->rows_processed)
    result.header_span = 1;
  return result;
}

static int detect_properties(const unsigned char *filepath,
                             struct zsv_file_properties *result,
                             int64_t detect_headerspan, /* reserved for future use */
                             int64_t detect_rows_to_skip, /* reserved for future use */
                             struct zsv_opts *opts) {
  (void)(detect_headerspan);
  (void)(detect_rows_to_skip);
  struct detect_properties_data data = { 0 };
  opts->row_handler = detect_properties_row;
  opts->ctx = &data;
  if(!strcmp((void *)filepath, "-"))
    opts->stream = stdin;
  else {
    opts->stream = fopen((const char *)filepath, "rb");
    if(!opts->stream) {
      perror((const char *)filepath);
      return 1;
    }
  }

  opts->keep_empty_header_rows = 1;
  data.parser = zsv_new(opts);
  while(!zsv_signal_interrupted && zsv_parse_more(data.parser) == zsv_status_ok)
    ;
  zsv_finish(data.parser);
  zsv_delete(data.parser);

  fclose(opts->stream);
  *result = guess_properties(&data);
  result->header_span += opts->header_span;
  result->skip += opts->rows_to_ignore;

  return 0;
}

#define ZSV_PROP_ARG_NONE -1
#define ZSV_PROP_ARG_AUTO -2
#define ZSV_PROP_ARG_REMOVE -3

static int prop_arg_value(int i, int argc, const char *argv[], int64_t *value) {
  if(i >= argc) {
    fprintf(stderr, "Option %s requires a value\n", argv[i-1]);
    return 1;
  }

  const char *arg = argv[i];
  if(!strcmp(arg, "auto"))
    *value = ZSV_PROP_ARG_AUTO;
  else if(!strcmp(arg, "none") || !strcmp(arg, "0") || !strcmp(arg, "-"))
    *value = ZSV_PROP_ARG_REMOVE;
  else {
    char *end = NULL;
    intmax_t j = strtoimax(arg, &end, 0);
    if(arg && *arg && end && *end == '\0') {
      if(j == 0)
        *value = ZSV_PROP_ARG_REMOVE;
      else if(j > 0 && j <= UINT_MAX)
        *value = j;
      return 0;
    }
    fprintf(stderr, "Invalid property value '%s'.\n", arg);
    fprintf(stderr, "Please use an integer greater than or equal to zero,"
            "'auto', 'none', or '-'\n");
    return 1;
  }
  return 0;
}

static int merge_properties(int64_t values[2],
                            struct zsv_file_properties *fp, char keep[2], int *remove_any
                            ) {
  int err = 0;
  // for each of d and R
  // if the corresponding argument > 0, save it
  // if the corresponding argument = ZSV_PROP_ARG_NONE, use fp value if fp is nonzero, else don't include it in the saved file
  // if the corersponding argument = ZSV_PROP_ARG_REMOVE
  // else error / unexpected value
  char fp_specified[2] = { fp->header_span_specified, fp->skip_specified };
  unsigned fp_val[2] = { fp->header_span, fp->skip };
  for(int i = 0; i < 2; i++) {
    switch(values[i]) {
    case ZSV_PROP_ARG_NONE:
      if(fp_specified[i]) {
        keep[i] = 1;
        values[i] = fp_val[i];
      }
      break;
    case ZSV_PROP_ARG_REMOVE:
      *remove_any = 1;
      values[i] = 0;
      break;
    default:
      if(values[i] >= 0)
        keep[i] = 1;
      else {
        fprintf(stderr, "save_properties: unexpected unhandled case!\n");
        err = 1;
      }
      break;
    }
  }
  return err;
}

// print_properties: return 1 if something was printed
static char print_properties_helper(FILE *f, int64_t values[2], char keep[2],
                                    const char *prop_id[2]) {
  char started = 0;
  for(int i = 0; i < 2; i++) {
    if(keep[i]) {
      if(!started) {
        fprintf(f, "{\n");
        started = 1;
      } else
        fprintf(f, ",\n");
      fprintf(f, "  \"%s\": %u", prop_id[i], (unsigned)values[i]);
    }
  }
  if(started)
    fprintf(f, "\n}\n");
  return started;
}

// print_properties: return 1 if something was printed
// print to stdout
// in addition, print to f, if provided
static char print_properties(FILE *f, int64_t values[2], char keep[2],
                             const char *prop_id[2]) {
  char result = 0;
  if(f)
    result = print_properties_helper(f, values, keep, prop_id);
  if(!print_properties_helper(stdout, values, keep, prop_id))
    printf("{}\n");
  return result;
}

static int merge_and_save_properties(const unsigned char *filepath,
                                     char save, char overwrite,
                                     int64_t d, int64_t R) {
  int err = 0;
  unsigned char *props_fn = zsv_cache_filepath(filepath, zsv_cache_type_property, 0, 0);
  if(!props_fn)
    err = 1;
  else {
    struct zsv_opts zsv_opts = { 0 };
    struct zsv_prop_handler custom_prop_handler = { 0 };
    struct zsv_file_properties fp = zsv_cache_load_props((const char *)filepath, &zsv_opts, &custom_prop_handler, NULL);
    err = fp.stat;
    if(!err) {
      if(save && !overwrite) {
        if((fp.header_span_specified && d)
           || (fp.skip_specified && R)) {
          fprintf(stderr, "Properties for this file already exist; use -f or --overwrite option to overwrite\n");
          err = 1;
        }
      }
    }

    if(!err) {
      unsigned char *props_fn_tmp = save ? zsv_cache_filepath(filepath, zsv_cache_type_property, 1, 1) : NULL;
      if(save && !props_fn_tmp)
        err = 1;
      else {
        // open a temp file, then write, then replace the orig file
        FILE *f = props_fn_tmp? fopen((char *)props_fn_tmp, "wb") : NULL;
        if(props_fn_tmp && !f) {
          perror((char *)props_fn_tmp);
          err = 1;
        } else {
          int64_t final_values[2] = { d, R };
          const char *prop_id[2] = { "header-row-span", "skip-head" };
          char keep[2] = { '\0', '\0' };
          int remove_any = 0;
          err = merge_properties(final_values, &fp, keep, &remove_any);
          char printed_something = 0;
          if(!err)
            printed_something = print_properties(f, final_values, keep, prop_id);
          if(f)
            fclose(f);

          if(!err) {
            if(save && f) {
              if(printed_something) {
                if(zsv_replace_file(props_fn_tmp, props_fn))
                  err = zsv_printerr(-1, "Unable to save %s", props_fn);
              } else if(remove_any)
                err = zsv_cache_remove(filepath, zsv_cache_type_property);
            }
          }
        }
        free(props_fn_tmp);
      }
    }
    free(props_fn);
  }
  return err;
}

enum zsv_prop_mode {
  zsv_prop_mode_default = 0,
  zsv_prop_mode_list_files = 'l',
  zsv_prop_mode_clean = 'K',
  zsv_prop_mode_export = 'e',
  zsv_prop_mode_import = 'i',
  zsv_prop_mode_copy = 'c',
  zsv_prop_mode_clear = 'r'
};

static enum zsv_prop_mode zsv_prop_get_mode(const char *opt) {
  if(!strcmp(opt, "--clean")) return zsv_prop_mode_clean;
  if(!strcmp(opt, "--list-files")) return zsv_prop_mode_list_files;
  if(!strcmp(opt, "--copy")) return zsv_prop_mode_copy;
  if(!strcmp(opt, "--export")) return zsv_prop_mode_export;
  if(!strcmp(opt, "--import")) return zsv_prop_mode_import;
  if(!strcmp(opt, "--clear")) return zsv_prop_mode_clear;
  return zsv_prop_mode_default;
}

struct prop_opts {
  int64_t d; // ZSV_PROP_ARG_AUTO, ZSV_PROP_ARG_REMOVE or > 0
  int64_t R; // ZSV_PROP_ARG_AUTO, ZSV_PROP_ARG_REMOVE or > 0
  unsigned char clear:1;
  unsigned char save:1;
  unsigned char overwrite:1;
  unsigned char _:3;
};

static int zsv_prop_execute_default(const unsigned char *filepath, struct zsv_opts zsv_opts, struct prop_opts opts) {
  int err = 0;
  struct zsv_file_properties fp = { 0 };
  if(opts.d >= 0 || opts.R >= 0 || opts.d == ZSV_PROP_ARG_REMOVE || opts.R == ZSV_PROP_ARG_REMOVE)
    opts.overwrite = 1;
  if(opts.d == ZSV_PROP_ARG_AUTO || opts.R == ZSV_PROP_ARG_AUTO) {
    err = detect_properties(filepath, &fp,
                            opts.d == ZSV_PROP_ARG_AUTO,
                            opts.R == ZSV_PROP_ARG_AUTO,
                            &zsv_opts);
  }

  if(!err) {
    if(opts.d == ZSV_PROP_ARG_AUTO)
      opts.d = fp.header_span;

    if(opts.R == ZSV_PROP_ARG_AUTO)
      opts.R = fp.skip;
    err = merge_and_save_properties(filepath, opts.save, opts.overwrite, opts.d, opts.R);
  }
  return err;
}

int zsv_is_prop_file(struct zsv_foreach_dirent_handle *h, size_t depth) {
  return depth == 1 && !strcmp(h->entry, "props.json");
}

#ifdef ZSV_IS_PROP_FILE_HANDLER
int ZSV_IS_PROP_FILE_HANDLER(struct zsv_foreach_dirent_handle *, size_t);
#endif

struct zsv_dir_filter *
zsv_prop_get_or_set_is_prop_file(
                                 int (*custom_is_prop_file)(struct zsv_foreach_dirent_handle *, size_t),
                                 int max_depth,
                                 char set
                                 ) {
  static struct zsv_dir_filter ctx = {
#ifndef ZSV_IS_PROP_FILE_HANDLER
    .filter = zsv_is_prop_file,
#else
    .filter = ZSV_IS_PROP_FILE_HANDLER,
#endif
#ifndef ZSV_IS_PROP_FILE_DEPTH
    .max_depth = 1
#else
    .max_depth = ZSV_IS_PROP_FILE_DEPTH
#endif
  };

  if(set) {
    if(!(ctx.filter = custom_is_prop_file)) {
      ctx.filter = zsv_is_prop_file;
      max_depth = 1;
    } else
      ctx.max_depth = max_depth;
  }
  return &ctx;
}

static int zsv_prop_foreach_list(struct zsv_foreach_dirent_handle *h, size_t depth) {
  if(!h->is_dir) {
    struct zsv_dir_filter *ctx = (struct zsv_dir_filter *)h->ctx;
    h->ctx = ctx->ctx;
    if(ctx->filter(h, depth))
      printf("%s\n", h->entry);
    h->ctx = ctx;
  }
  return 0;
}

zsv_foreach_dirent_handler
zsv_prop_get_or_set_is_prop_dir(
                                int (*custom_is_prop_dir)(struct zsv_foreach_dirent_handle *, size_t),
                                char set
                                ) {
  static int (*func)(struct zsv_foreach_dirent_handle *, size_t) = NULL;
  if(set)
    func = custom_is_prop_dir;
  return func;
}

static int zsv_prop_execute_list_files(const unsigned char *filepath, char verbose) {
  int err = 0;
  unsigned char *cache_path = zsv_cache_path(filepath, NULL, 0);
  struct zsv_dir_filter ctx = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
  if(cache_path) {
    zsv_foreach_dirent((const char *)cache_path, ctx.max_depth, zsv_prop_foreach_list,
                       &ctx, verbose);
    free(cache_path);
  }
  return err;
}

struct zsv_prop_foreach_clean_ctx {
  const char *dirpath;
  unsigned char dry;
};

static int zsv_prop_foreach_clean(struct zsv_foreach_dirent_handle *h, size_t depth) {
  int err = 0;
  if(depth == 1) {
    struct zsv_prop_foreach_clean_ctx *ctx = h->ctx;
    if(h->is_dir) {
      // h->entry is the name of the top-level file that this folder relates to
      // make sure that the top-level file exists
      h->no_recurse = 1;

      char *cache_owner_path;
      asprintf(&cache_owner_path, "%s%c%s", ctx->dirpath, FILESLASH, h->entry);
      if(!cache_owner_path) {
        fprintf(stderr, "Out of memory!\n");
        return 1;
      }
      if(!zsv_file_exists(cache_owner_path)) {
        if(ctx->dry)
          printf("Orphaned: %s\n", h->parent_and_entry);
        else
          err = zsv_remove_dir_recursive((const unsigned char *)h->parent_and_entry);
      }
      free(cache_owner_path);
    } else {
      // there should be no files at depth 1, so just delete
      if(ctx->dry)
        printf("Unrecognized: %s\n", h->parent_and_entry);
      else if(unlink(h->parent_and_entry)) {
        perror(h->parent_and_entry);
        err = 1;
      }
    }
  }
  return err;
}

enum zsv_prop_foreach_copy_mode {
  zsv_prop_foreach_copy_mode_check = 1,
  zsv_prop_foreach_copy_mode_copy
};

struct zsv_prop_foreach_copy_ctx {
  struct zsv_dir_filter zsv_dir_filter;
  const unsigned char *src_cache_dir;
  const unsigned char *dest_cache_dir;
  enum zsv_prop_foreach_copy_mode mode;
  int err;
  unsigned char output_started:1;
  unsigned char force:1;
  unsigned char dry:1;
  unsigned char _:5;
};

static int zsv_prop_foreach_copy(struct zsv_foreach_dirent_handle *h, size_t depth) {
  if(!h->is_dir) {
    struct zsv_prop_foreach_copy_ctx *ctx = h->ctx;
    h->ctx = ctx->zsv_dir_filter.ctx;
    if(ctx->zsv_dir_filter.filter(h, depth)) {
      char *dest_prop_filepath;
      asprintf(&dest_prop_filepath, "%s%s", ctx->dest_cache_dir, h->parent_and_entry + strlen((const char *)ctx->src_cache_dir));
      if(!dest_prop_filepath) {
        ctx->err = errno = ENOMEM;
        perror(NULL);
      } else {
        switch(ctx->mode) {
        case zsv_prop_foreach_copy_mode_check:
          {
            if(!zsv_file_readable(h->parent_and_entry, &ctx->err, NULL)) { // check if source is not readable
              perror(h->parent_and_entry);
            } else if(!ctx->force && access(dest_prop_filepath, F_OK) != -1) { // check if dest already exists
              ctx->err = EEXIST;
              if(!ctx->output_started) {
                ctx->output_started = 1;
                const char *msg = strerror(EEXIST);
                fprintf(stderr, "%s:\n", msg ? msg : "File already exists");
              }
              fprintf(stderr, "  %s\n", dest_prop_filepath);
            } else if(ctx->dry)
              printf("%s => %s\n", h->parent_and_entry, dest_prop_filepath);
          }
          break;
        case zsv_prop_foreach_copy_mode_copy:
          if(!ctx->dry) {
            char *dest_prop_filepath_tmp;
            asprintf(&dest_prop_filepath_tmp, "%s.temp", dest_prop_filepath);
            if(!dest_prop_filepath_tmp) {
              ctx->err = errno = ENOMEM;
              perror(NULL);
            } else {
              if(h->verbose)
                fprintf(stderr, "Copying temp: %s => %s\n", h->parent_and_entry, dest_prop_filepath_tmp);
              int err = zsv_copy_file(h->parent_and_entry, dest_prop_filepath_tmp);
              if(err)
                ctx->err = err;
              else {
                if(h->verbose)
                  fprintf(stderr, "Renaming: %s => %s\n", dest_prop_filepath_tmp, dest_prop_filepath);
                if(zsv_replace_file(dest_prop_filepath_tmp, dest_prop_filepath)) {
                  const char *msg = strerror(errno);
                  fprintf(stderr, "Unable to rename %s -> %s: ", dest_prop_filepath_tmp, dest_prop_filepath);
                  zsv_perror(NULL);
                  ctx->err = errno;
                }
              }
              free(dest_prop_filepath_tmp);
            }
          }
          break;
        }
        free(dest_prop_filepath);
      }
    }
    h->ctx = ctx;
  }
  return 0;
}

static int zsv_prop_execute_copy(const char *src, const char *dest, unsigned char force, unsigned char dry, unsigned char verbose) {
  int err = 0;
  unsigned char *src_cache_dir = zsv_cache_path((const unsigned char *)src, NULL, 0);
  unsigned char *dest_cache_dir = zsv_cache_path((const unsigned char *)dest, NULL, 0);

  if(!(src_cache_dir && dest_cache_dir))
    err = errno = ENOMEM, perror(NULL);
  else {
    // if !force, only proceed if:
    // - src exists (file)
    // - dest exists (file)
    // - dest file property cache d.n. have conflicts
    struct zsv_prop_foreach_copy_ctx ctx = { 0 };
    ctx.zsv_dir_filter = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
    ctx.dest_cache_dir = dest_cache_dir;
    ctx.src_cache_dir = src_cache_dir;
    ctx.force = force;
    ctx.dry = dry;

    if(!force) {
      if(!zsv_file_exists(src))
        err = errno = ENOENT, perror(src);
      if(!zsv_file_exists(dest))
        err = errno = ENOENT, perror(dest);
    }

    if(!err) {
      // for each property file, check if dest has same-named property file
      ctx.mode = zsv_prop_foreach_copy_mode_check;
      zsv_foreach_dirent((const char *)src_cache_dir, ctx.zsv_dir_filter.max_depth, zsv_prop_foreach_copy,
                         &ctx, verbose);
    }

    if(!err && !(ctx.err && !force)) {
      // copy the files
      ctx.mode = zsv_prop_foreach_copy_mode_copy;
      zsv_foreach_dirent((const char *)src_cache_dir, ctx.zsv_dir_filter.max_depth, zsv_prop_foreach_copy,
                         &ctx, verbose);
    }
  }
  free(src_cache_dir);
  free(dest_cache_dir);
  return err;
}

static int zsv_prop_execute_clean(const char *dirpath, unsigned char dry, unsigned char verbose) {
  // TO DO: if ZSV_CACHE_DIR-tmp exists, delete it (file or dir)
  int err = 0;
  size_t dirpath_len = strlen(dirpath);
  while(dirpath_len && memchr("/\\", dirpath[dirpath_len-1], 2) != NULL)
    dirpath_len--;
  if(!dirpath_len)
    return 0;

  // TO DO: if NO_STDIN, require --force, else prompt user

  char *cache_parent;
  if(!strcmp(dirpath, "."))
    cache_parent = strdup(ZSV_CACHE_DIR);
  else
    asprintf(&cache_parent, "%.*s%c%s", (int)dirpath_len, dirpath, FILESLASH, ZSV_CACHE_DIR);
  if(!cache_parent) {
    fprintf(stderr, "Out of memory!\n");
    return 1;
  }

  struct zsv_prop_foreach_clean_ctx ctx = { 0 };
  ctx.dirpath = dirpath;
  ctx.dry = dry;

  zsv_foreach_dirent(cache_parent, 0, zsv_prop_foreach_clean, &ctx, verbose);
  free(cache_parent);
  return err;
}

static int zsv_prop_execute_export(const char *src, const char *dest, unsigned char verbose) {
  int err = 0;
  unsigned char *parent_dir = zsv_cache_path((const unsigned char *)src, NULL, 0);
  if(!(parent_dir))
    err = errno = ENOMEM, perror(NULL);
  else {
    struct zsv_dir_filter zsv_dir_filter = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
    err = zsv_dir_to_json(parent_dir, (const unsigned char *)dest, &zsv_dir_filter, verbose);
  }
  free(parent_dir);
  return err;
}

static int zsv_prop_execute_import(const char *dest, const char *src, unsigned char force,
                                   unsigned char dry, unsigned char verbose) {
  int err = 0;
  unsigned char *target_dir = NULL;
  FILE *fsrc = NULL;
  if(!force && !zsv_file_exists(dest)) {
    err = errno = ENOENT;
    perror(dest);
  } else if(!(target_dir = zsv_cache_path((const unsigned char *)dest, NULL, 0))) {
    err = errno = ENOMEM;
    perror(NULL);
  } else if(!(fsrc = src ? fopen(src, "rb") : stdin)) {
    err = errno;
    perror(src);
  } else {
    int flags = (force ? ZSV_DIR_FLAG_FORCE : 0) | (dry ? ZSV_DIR_FLAG_DRY : 0);
    err = zsv_dir_from_json(target_dir, fsrc, flags, verbose);
  }
  if(fsrc && fsrc != stdin)
    fclose(fsrc);
  free(target_dir);
  return err;
}

int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int m_argc, const char *m_argv[]) {
  int err = 0;
  char verbose = 0;
  if(m_argc < 2 ||
     (m_argc > 1 && (!strcmp(m_argv[1], "-h") || !strcmp(m_argv[1], "--help"))))
    err = zsv_property_usage(stdout);
  else {
    struct prop_opts opts = { 0 };
    opts.d = ZSV_PROP_ARG_NONE;
    opts.R = ZSV_PROP_ARG_NONE;

    const unsigned char *filepath = (const unsigned char *)m_argv[1];
    if(m_argc == 2)
      return show_all_properties(filepath);

    enum zsv_prop_mode mode = zsv_prop_mode_default;
    unsigned char dry = 0;
    const char *mode_arg = NULL;   // e.g. "--export"
    const char *mode_value = NULL; // e.g. "saved_export.json"
    for(int i = 2; !err && i < m_argc; i++) {
      const char *opt = m_argv[i];
      if(!strcmp(opt, "-v") || !strcmp(opt, "--verbose"))
        verbose = 1;
      else if(!strcmp(opt, "-d") || !strcmp(opt, "--header-row-span"))
        err = prop_arg_value(++i, m_argc, m_argv, &opts.d);
      else if(!strcmp(opt, "-R") || !strcmp(opt, "--skip-head"))
        err = prop_arg_value(++i, m_argc, m_argv, &opts.R);
      else if(zsv_prop_get_mode(opt)) {
        if(mode_arg)
          err = fprintf(stderr, "Option %s cannot be used together with %s\n", opt, mode_arg);
        else {
          mode = zsv_prop_get_mode(opt);
          mode_arg = opt;
          if(mode == zsv_prop_mode_export || mode == zsv_prop_mode_import || mode == zsv_prop_mode_copy) {
            if(++i < m_argc)
              mode_value = m_argv[i];
            else
              err = fprintf(stderr, "Option %s requires a value\n", opt);
          }
        }
      } else if(!strcmp(opt, "--auto")) {
        if(opts.d != ZSV_PROP_ARG_NONE && opts.R != ZSV_PROP_ARG_NONE)
          err = fprintf(stderr, "--auto specified, but all other properties also specified");
        else {
          if(opts.d == ZSV_PROP_ARG_NONE)
            opts.d = ZSV_PROP_ARG_AUTO;
          if(opts.R == ZSV_PROP_ARG_NONE)
            opts.R = ZSV_PROP_ARG_AUTO;
        }
      } else if(!strcmp(opt, "--save"))
        opts.save = 1;
      else if(!strcmp(opt, "-f") || !strcmp(opt, "--overwrite"))
        opts.overwrite = 1;
      else if(!strcmp(opt, "--dry"))
        dry = 1;
      else {
        fprintf(stderr, "Unrecognized option: %s\n", opt);
        err = 1;
      }
    }

    // check if option combination is invalid
    // to do: check with zsv_prop_mode_clear
    if(!err) {
      char have_auto = opts.d == ZSV_PROP_ARG_AUTO || opts.R == ZSV_PROP_ARG_AUTO;
      char have_specified = opts.d >= 0 || opts.R >= 0;
      char have_remove = opts.d == ZSV_PROP_ARG_REMOVE || opts.R == ZSV_PROP_ARG_REMOVE;

      if(have_auto && (have_specified || have_remove)) {
        fprintf(stderr, "Non-auto options may not be mixed with auto options\n");
        err = 1;
      } else if((have_auto || have_specified || have_remove || opts.save)
                && mode != zsv_prop_mode_default)
        err = fprintf(stderr, "Invalid options in combination with %s\n", mode_arg);

      if(have_specified || have_remove) {
        opts.save = 1;
        opts.overwrite = 1;
      }
    }

    if(!err) {
      switch(mode) {
      case zsv_prop_mode_clear:
        if(!(filepath && *filepath))
          err = fprintf(stderr, "--clear: please specify an input file\n");
        else {
          struct prop_opts opts2 = { 0 };
          opts2.d = ZSV_PROP_ARG_NONE;
          opts2.R = ZSV_PROP_ARG_NONE;
          if(memcmp(&opts, &opts2, sizeof(opts)))
            err = fprintf(stderr, "--clear cannot be used in conjunction with any other options\n");
          else {
            unsigned char *cache_path = zsv_cache_path(filepath, NULL, 0);
            if(!cache_path)
              err = ENOMEM;
            else if(zsv_dir_exists((const char *)cache_path))
              err = zsv_remove_dir_recursive(cache_path);
            free(cache_path);
          }
        }
        break;
      case zsv_prop_mode_list_files:
        err = zsv_prop_execute_list_files(filepath, verbose);
        break;
      case zsv_prop_mode_clean:
        err = zsv_prop_execute_clean((const char *)filepath, dry, verbose);
        break;
      case zsv_prop_mode_copy:
        err = zsv_prop_execute_copy((const char *)filepath, mode_value, opts.overwrite, dry, verbose);
        break;
      case zsv_prop_mode_export:
        err = zsv_prop_execute_export((const char *)filepath, mode_value && strcmp(mode_value, "-") ? mode_value : NULL, verbose);
        break;
      case zsv_prop_mode_import:
        err = zsv_prop_execute_import((const char *)filepath, mode_value && strcmp(mode_value, "-") ? mode_value : NULL, opts.overwrite, dry, verbose);
        break;
      case zsv_prop_mode_default:
        {
          struct zsv_opts zsv_opts;
          zsv_args_to_opts(m_argc, m_argv, &m_argc, m_argv, &zsv_opts, NULL);
          err = zsv_prop_execute_default(filepath, zsv_opts, opts);
        }
        break;
      }
    }
  }
  return err;
}
