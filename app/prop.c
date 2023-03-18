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
#include <yajl_helper.h>
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
  "    when using --clean: directory to clean from (. for current directory)",
  "  and options may be one or more of:",
  "    -d,--header-row-span <value>: set/unset/auto-detect header depth (see below)",
  "    -R,--skip-head <value>      : set/unset/auto-detect initial rows to skip (see below)",
  "    --list-files                : list all property sets associted with the given file", // output a list of all cache files
  "    --clear                     : delete all properties of given files",
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
    struct zsv_file_properties fp = { 0 };
    struct zsv_opts zsv_opts = { 0 };
    err = zsv_cache_load_props((const char *)filepath, &zsv_opts, &fp, NULL);
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
  zsv_prop_mode_copy = 'c'
};

static enum zsv_prop_mode zsv_prop_get_mode(const char *opt) {
  if(!strcmp(opt, "--clean")) return zsv_prop_mode_clean;
  if(!strcmp(opt, "--list-files")) return zsv_prop_mode_list_files;
  if(!strcmp(opt, "--copy")) return zsv_prop_mode_copy;
  if(!strcmp(opt, "--export")) return zsv_prop_mode_export;
  if(!strcmp(opt, "--import")) return zsv_prop_mode_import;
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

struct is_property_ctx {
  zsv_foreach_dirent_handler handler;
  size_t max_depth;
};

#ifdef ZSV_IS_PROP_FILE_HANDLER
int ZSV_IS_PROP_FILE_HANDLER(struct zsv_foreach_dirent_handle *, size_t);
#endif

struct is_property_ctx *
zsv_prop_get_or_set_is_prop_file(
                                 int (*custom_is_prop_file)(struct zsv_foreach_dirent_handle *, size_t),
                                 int max_depth,
                                 char set
                                 ) {
  static struct is_property_ctx ctx = {
#ifndef ZSV_IS_PROP_FILE_HANDLER
    .handler = zsv_is_prop_file,
#else
    .handler = ZSV_IS_PROP_FILE_HANDLER,
#endif
#ifndef ZSV_IS_PROP_FILE_DEPTH
    .max_depth = 1
#else
    .max_depth = ZSV_IS_PROP_FILE_DEPTH
#endif
  };

  if(set) {
    if(!(ctx.handler = custom_is_prop_file)) {
      ctx.handler = zsv_is_prop_file;
      max_depth = 1;
    } else
      ctx.max_depth = max_depth;
  }
  return &ctx;
}

static int zsv_prop_foreach_list(struct zsv_foreach_dirent_handle *h, size_t depth) {
  if(!h->is_dir) {
    struct is_property_ctx *ctx = (struct is_property_ctx *)h->ctx;
    if(ctx->handler(h, depth))
      printf("%s\n", h->entry);
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
  struct is_property_ctx ctx = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
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
  struct is_property_ctx is_property_ctx;
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
    if(ctx->is_property_ctx.handler(h, depth)) {
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
                if(rename(dest_prop_filepath_tmp, dest_prop_filepath)) {
                  const char *msg = strerror(errno);
                  fprintf(stderr, "Unable to rename %s -> %s: %s\n", dest_prop_filepath_tmp, dest_prop_filepath, msg ? msg : "Unknown error");
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
  }
  return 0;
}

struct zsv_prop_foreach_export_ctx {
  struct is_property_ctx is_property_ctx;
  const unsigned char *src_cache_dir;
  struct jv_to_json_ctx jctx;
  zsv_jq_handle zjq;
  unsigned count; // number of files exported so far
  int err;
};

static int zsv_prop_foreach_export(struct zsv_foreach_dirent_handle *h, size_t depth) {
  if(!h->is_dir) {
    struct zsv_prop_foreach_export_ctx *ctx = h->ctx;
    if(ctx->is_property_ctx.handler(h, depth) && !ctx->err) {
      char suffix = 0;
      if(strlen(h->parent_and_entry) > 5 && !zsv_stricmp((const unsigned char *)h->parent_and_entry + strlen(h->parent_and_entry) - 5, (const unsigned char *)".json"))
        suffix = 'j'; // json
      else if(strlen(h->parent_and_entry) > 4 && !zsv_stricmp((const unsigned char *)h->parent_and_entry + strlen(h->parent_and_entry) - 4, (const unsigned char *)".txt"))
        suffix = 't'; // text
      if(suffix) {
        // for now, only handle json or txt
        FILE *f = fopen(h->parent_and_entry, "rb");
        if(!f)
          perror(h->parent_and_entry);
        else {
          // create an entry for this file. the map key is the file name; its value is the file contents
          unsigned char *js = zsv_json_from_str((const unsigned char *)h->parent_and_entry + strlen((const char *)ctx->src_cache_dir) + 1);
          if(!js)
            errno = ENOMEM, perror(NULL);
          else if(*js) {
            if(ctx->count > 0)
              if(zsv_jq_parse(ctx->zjq, ",", 1))
                ctx->err = 1;
            if(!ctx->err) {
              ctx->count++;
              if(zsv_jq_parse(ctx->zjq, js, strlen((const char *)js)) || zsv_jq_parse(ctx->zjq, ":", 1))
                ctx->err = 1;
              else {
                switch(suffix) {
                case 'j': // json
                  if(zsv_jq_parse_file(ctx->zjq, f))
                    ctx->err = 1;
                  break;
                case 't': // txt
                  // for now we are going to limit txt file values to 4096 chars and JSON-stringify it
                  {
                    unsigned char buff[4096];
                    size_t n = fread(buff, 1, sizeof(buff), f);
                    unsigned char *txt_js = NULL;
                    if(n) {
                      txt_js = zsv_json_from_str_n(buff, n);
                      if(zsv_jq_parse(ctx->zjq, txt_js ? txt_js : (const unsigned char *)"null", txt_js ? strlen((const char *)txt_js) : 4))
                        ctx->err = 1;
                    }
                  }
                  break;
                }
              }
            }
          }
          free(js);
          fclose(f);
        }
      }
    }
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
    ctx.is_property_ctx = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
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
      zsv_foreach_dirent((const char *)src_cache_dir, ctx.is_property_ctx.max_depth, zsv_prop_foreach_copy,
                         &ctx, verbose);
    }

    if(!err && !(ctx.err && !force)) {
      // copy the files
      ctx.mode = zsv_prop_foreach_copy_mode_copy;
      zsv_foreach_dirent((const char *)src_cache_dir, ctx.is_property_ctx.max_depth, zsv_prop_foreach_copy,
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
  unsigned char *src_cache_dir = zsv_cache_path((const unsigned char *)src, NULL, 0);
  if(!(src_cache_dir))
    err = errno = ENOMEM, perror(NULL);
  else {
    FILE *fdest = dest ? fopen(dest, "wb") : stdout;
    if(!fdest)
      err = errno, perror(dest);
    else {
      struct zsv_prop_foreach_export_ctx ctx = { 0 };
      ctx.is_property_ctx = *zsv_prop_get_or_set_is_prop_file(NULL, 0, 0);
      ctx.src_cache_dir = src_cache_dir;

      // use a jq filter to pretty-print
      ctx.jctx.write1 = zsv_jq_fwrite1;
      ctx.jctx.ctx = fdest;
      ctx.jctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;
      enum zsv_jq_status jqstat;
      ctx.zjq = zsv_jq_new((const unsigned char *)".", jv_to_json_func, &ctx.jctx, &jqstat);
      if(!ctx.zjq)
        err = 1, fprintf(stderr, "zsv_jq_new\n");
      else {
        if(jqstat == zsv_jq_status_ok && zsv_jq_parse(ctx.zjq, "{", 1) == zsv_jq_status_ok) {
          // export each file
          zsv_foreach_dirent((const char *)src_cache_dir, ctx.is_property_ctx.max_depth, zsv_prop_foreach_export,
                             &ctx, verbose);
          if(!ctx.err && zsv_jq_parse(ctx.zjq, "}", 1))
            ctx.err = 1;
          if(!ctx.err && zsv_jq_finish(ctx.zjq))
            ctx.err = 1;
          zsv_jq_delete(ctx.zjq);
        }
        err = ctx.err;
      }
      fclose(fdest);
    }
  }
  free(src_cache_dir);
  return err;
}

struct prop_import_ctx {
  const char *filepath_prefix;
  unsigned char buff[4096];
  size_t content_start;
  FILE *out;
  char *out_filepath;
  struct jv_to_json_ctx jctx;
  zsv_jq_handle zjq;

  int err;
  unsigned char in_obj:1;
  unsigned char do_check:1;
  unsigned char dry:1;
  unsigned char _:5;
};

static void prop_import_close_out(struct prop_import_ctx *ctx) {
  if(ctx->zjq) {
    zsv_jq_finish(ctx->zjq);
    zsv_jq_delete(ctx->zjq);
    ctx->zjq = NULL;
  }
  if(ctx->out) {
    fclose(ctx->out);
    ctx->out = NULL;
    free(ctx->out_filepath);
    ctx->out_filepath = NULL;
  }
}

static int prop_import_map_key(struct yajl_helper_parse_state *st,
                               const unsigned char *s, size_t len) {
  if(yajl_helper_level(st) == 1 && len) { // new property file entry
    struct prop_import_ctx *ctx = yajl_helper_data(st);

    char *fn = NULL;
    if(ctx->filepath_prefix)
      asprintf(&fn, "%s%c%.*s", ctx->filepath_prefix, FILESLASH, (int)len, s);
    else
      asprintf(&fn, "%.*s", (int)len, s);
    if(!fn) {
      errno = ENOMEM;
      perror(NULL);
    } else if(ctx->do_check) {
      // we just want to check if the destination file exists
      if(access(fn, F_OK) != -1) { // it exists
        ctx->err = errno = EEXIST;
        perror(fn);
      }
    } else if(ctx->dry) { // just output the name of the file
      printf("%s\n", fn);
    } else if(zsv_mkdirs(fn, 1)) {
      fprintf(stderr, "Unable to create directories for %s\n", fn);
    } else if(!((ctx->out = fopen(fn, "wb")))) {
      perror(fn);
    } else {
      ctx->out_filepath = fn;
      fn = NULL;

      // if it's a JSON file, use a jq filter to pretty-print
      if(strlen(ctx->out_filepath) > 5 && !zsv_stricmp((const unsigned char *)ctx->out_filepath + strlen(ctx->out_filepath) - 5, (const unsigned char *)".json")) {
        ctx->jctx.write1 = zsv_jq_fwrite1;
        ctx->jctx.ctx = ctx->out;
        ctx->jctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;
        enum zsv_jq_status jqstat;
        ctx->zjq = zsv_jq_new((const unsigned char *)".", jv_to_json_func, &ctx->jctx, &jqstat);
        if(!ctx->zjq) {
          fprintf(stderr, "zsv_jq_new: unable to open for %s\n", ctx->out_filepath);
          prop_import_close_out(ctx);
        }
      }
    }
    free(fn);
  }
  return 1;
}

static int prop_import_start_obj(struct yajl_helper_parse_state *st) {
  if(yajl_helper_level(st) == 2) {
    struct prop_import_ctx *ctx = yajl_helper_data(st);
    ctx->in_obj = 1;
    ctx->content_start = yajl_get_bytes_consumed(st->yajl) - 1;
  }
  return 1;
}

// prop_import_flush(): return err
static int prop_import_flush(yajl_handle yajl, struct prop_import_ctx *ctx) {
  if(ctx->zjq) {
    size_t current_position = yajl_get_bytes_consumed(yajl);
    if(current_position <= ctx->content_start)
      fprintf(stderr, "Error! prop_import_flush unexpected current position\n");
    else
      zsv_jq_parse(ctx->zjq, ctx->buff + ctx->content_start,
                   current_position - ctx->content_start);
    ctx->content_start = 0;
  }
  return 0;
}

static int prop_import_end_obj(struct yajl_helper_parse_state *st) {
  if(yajl_helper_level(st) == 1) { // just finished level 2
    struct prop_import_ctx *ctx = yajl_helper_data(st);
    prop_import_flush(st->yajl, yajl_helper_data(st));
    prop_import_close_out(ctx);
    ctx->in_obj = 0;
  }
  return 1;
}

static int prop_import_process_value(struct yajl_helper_parse_state *st,
                                     struct json_value *value) {
  if(yajl_helper_level(st) == 1) { // just finished level 2
    struct prop_import_ctx *ctx = yajl_helper_data(st);
    const unsigned char *jsstr;
    size_t len;
    json_value_default_string(value, &jsstr, &len);
    if(ctx->zjq) {
      unsigned char *js = len ? zsv_json_from_str_n(jsstr, len) : NULL;
      if(js)
        zsv_jq_parse(ctx->zjq, js, strlen((char *)js));
      else
        zsv_jq_parse(ctx->zjq, "null", 4);
      free(js);
    } else if(len && ctx->out)
      fwrite(jsstr, 1, len, ctx->out);
    prop_import_close_out(ctx);
  }
  return 1;
}


static int zsv_prop_execute_import(const char *dest, const char *src, unsigned char force, unsigned char dry, unsigned char verbose) {
  unsigned char *dest_cache_dir = NULL;
  FILE *fsrc = NULL;
  int err = 0;

  if(!force && !zsv_file_exists(dest)) {
    err = errno = ENOENT;
    perror(dest);
  } else if(!(dest_cache_dir = zsv_cache_path((const unsigned char *)dest, NULL, 0))) {
    err = errno = ENOMEM;
    perror(NULL);
  } else if(!(fsrc = src ? fopen(src, "rb") : stdin)) {
    err = errno;
    perror(src);
  } else {
    char *tmp_fn = NULL;
    if(!force) {
      // if input is stdin, we'll need to read it twice, so save it first
      // this isn't the most efficient way to do it, as it reads it 3 times
      // but it's easier and the diff is immaterial
      if(fsrc == stdin) {
        fsrc = NULL;
        tmp_fn = zsv_get_temp_filename("zsv_prop_XXXXXXXX");
        src = (const char *)tmp_fn;
        FILE *tmp_f;
        if(!tmp_fn) {
          err = errno = ENOMEM;
          perror(NULL);
        } else if(!(tmp_f = fopen(tmp_fn, "wb"))) {
          err = errno;
          perror(tmp_fn);
        } else {
          err = zsv_copy_file_ptr(stdin, tmp_f);
          fclose(tmp_f);
          if(!(fsrc = fopen(tmp_fn, "rb"))) {
            err = errno;
            perror(tmp_fn);
          }
        }
      }
    }

    if(!err) {
      // we will run this loop either once (force) or twice (no force):
      // 1. check before running (no force)
      // 2. do the import
      char do_check = !force;

      if(do_check && !zsv_dir_exists((const char *)dest_cache_dir))
        do_check = 0;

      for(int i = do_check ? 0 : 1; i < 2 && !err; i++) {
        do_check = i == 0;

        size_t bytes_read;
        struct yajl_helper_parse_state st;
        struct prop_import_ctx ctx = { 0 };
        ctx.filepath_prefix = (const char *)dest_cache_dir;

        int (*start_obj)(struct yajl_helper_parse_state *st) = NULL;
        int (*end_obj)(struct yajl_helper_parse_state *st) = NULL;
        int (*process_value)(struct yajl_helper_parse_state *, struct json_value *) = NULL;

        if(do_check)
          ctx.do_check = do_check;
        else {
          ctx.dry = dry;
          if(!ctx.dry) {
            start_obj = prop_import_start_obj;
            end_obj = prop_import_end_obj;
            process_value = prop_import_process_value;
          }
        }

        if(yajl_helper_parse_state_init(&st, 32,
                                        start_obj, end_obj, // map start/end
                                        prop_import_map_key,
                                        start_obj, end_obj, // array start/end
                                        process_value,
                                        &ctx) != yajl_status_ok) {
          err = errno = ENOMEM;
          perror(NULL);
        } else {
          while((bytes_read = fread(ctx.buff, 1, sizeof(ctx.buff), fsrc)) > 0) {
            if(yajl_parse(st.yajl, ctx.buff, bytes_read) != yajl_status_ok)
              yajl_helper_print_err(st.yajl, ctx.buff, bytes_read);
            if(ctx.in_obj)
              prop_import_flush(st.yajl, &ctx);
          }
          if(yajl_complete_parse(st.yajl) != yajl_status_ok)
            yajl_helper_print_err(st.yajl, ctx.buff, bytes_read);

          if(ctx.out) { // e.g. if bad JSON and parse failed
            fclose(ctx.out);
            free(ctx.out_filepath);
          }
        }
        yajl_helper_parse_state_free(&st);

        if(ctx.err)
          err = ctx.err;
        if(i == 0) {
          rewind(fsrc);
          if(errno) {
            err = errno;
            perror(NULL);
          }
        }
      }
    }
    if(tmp_fn) {
      unlink(tmp_fn);
      free(tmp_fn);
    }
  }

  if(fsrc && fsrc != stdin)
    fclose(fsrc);
  free(dest_cache_dir);

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

    if(m_argc == 3 && !strcmp("--clear", m_argv[2]))
      return zsv_cache_remove(filepath, zsv_cache_type_property);

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
      } else if(!strcmp(opt, "--clear"))
        err = fprintf(stderr, "--clear cannot be used in conjunction with any other options\n");
      else if(!strcmp(opt, "--auto")) {
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
