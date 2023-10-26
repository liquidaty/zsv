/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <assert.h>
#ifdef _WIN32
#define _CRT_RAND_S // for random number generator, used when sampling. must come before including stdlib.h
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

#define ZSV_COMMAND select_pull
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/string.h>
#include <zsv/utils/mem.h>

struct zsv_select_search_str {
  struct zsv_select_search_str *next;
  const char *value;
  size_t len;
};

static void zsv_select_search_str_delete(struct zsv_select_search_str *ss) {
  for(struct zsv_select_search_str *next; ss; ss = next) {
    next = ss->next;
    free(ss);
  }
}

struct zsv_select_uint_list {
  struct zsv_select_uint_list *next;
  unsigned int value;
};

struct zsv_select_data {
  FILE *in;
  unsigned int current_column_ix;
  size_t data_row_count;

  struct zsv_opts *opts;
  unsigned int errcount;

  unsigned int output_col_index; // num of cols printed in current row

  // output columns:
  const char **col_argv;
  int col_argc;
  char *cols_to_print; // better: bitfield

  struct {
    unsigned int ix; // index of the input column to be output
    struct { // merge data: only used with --merge
      struct zsv_select_uint_list *indexes, **last_index;
    } merge;
  } *out2in; // array of .output_cols_count length; out2in[x] = y where x = output ix, y = input info

  unsigned int output_cols_count; // total count of output columns

#define MAX_EXCLUSIONS 1024
  const unsigned char *exclusions[MAX_EXCLUSIONS];
  unsigned int exclusion_count;

  unsigned int header_name_count;
  unsigned char **header_names;

  const char *prepend_header; // --prepend-header

  char header_finished;

  char embedded_lineend;

  double sample_pct;

  unsigned char sample_every_n;

  size_t data_rows_limit;
  size_t skip_data_rows;

  struct zsv_select_search_str *search_strings;

  zsv_csv_writer csv_writer;

  size_t overflow_size;

  /*
  struct {
    size_t *offsets;
    size_t count;
  } fixed;
  */
  unsigned char whitspace_clean_flags;

  unsigned char print_all_cols:1;
  unsigned char use_header_indexes:1;
  unsigned char no_trim_whitespace:1;
  unsigned char cancelled:1;
  unsigned char skip_this_row:1;
  unsigned char verbose:1;
  unsigned char clean_white:1;
  unsigned char prepend_line_number:1;

  unsigned char any_clean:1;
#define ZSV_SELECT_DISTINCT_MERGE 2
  unsigned char distinct:2; // 1 = ignore subsequent cols, ZSV_SELECT_DISTINCT_MERGE = merge subsequent cols (first non-null value)

  unsigned char no_header:1;
  unsigned char _:4;
};

enum zsv_select_column_index_selection_type {
  zsv_select_column_index_selection_type_none = 0,
  zsv_select_column_index_selection_type_single,
  zsv_select_column_index_selection_type_range,
  zsv_select_column_index_selection_type_lower_bounded
};

static enum zsv_select_column_index_selection_type
zsv_select_column_index_selection(const unsigned char *arg, unsigned *lo, unsigned *hi);

static inline void zsv_select_add_exclusion(struct zsv_select_data *data, const char *name) {
  if(data->exclusion_count < MAX_EXCLUSIONS)
    data->exclusions[data->exclusion_count++] = (const unsigned char *)name;
}

static inline unsigned char *zsv_select_get_header_name(struct zsv_select_data *data, unsigned in_ix) {
  if(in_ix < data->header_name_count)
    return data->header_names[in_ix];
  return NULL;
}

static inline char zsv_select_excluded_current_header_name(struct zsv_select_data *data, unsigned in_ix) {
  if(data->exclusion_count) {
    unsigned char *header_name = zsv_select_get_header_name(data, in_ix);
    if(data->use_header_indexes) {
      for(unsigned int ix = 0; ix < data->exclusion_count; ix++) {
        unsigned i, j;
        switch(zsv_select_column_index_selection(data->exclusions[ix], &i, &j)) {
        case zsv_select_column_index_selection_type_none:
          // not expected!
          break;
        case zsv_select_column_index_selection_type_single:
          if(in_ix + 1 == i) return 1;
          break;
        case zsv_select_column_index_selection_type_range:
          if(i <= in_ix + 1 && in_ix + 1 <= j) return 1;
          break;
        case zsv_select_column_index_selection_type_lower_bounded:
          if(i <= in_ix + 1) return 1;
          break;
        }
      }
    } else {
      if(header_name) {
        for(unsigned int i = 0; i < data->exclusion_count; i++)
          if(!zsv_stricmp(header_name, data->exclusions[i]))
            return 1;
      }
    }
  }
  return 0;
}

// zsv_select_find_header(): return 1-based index, or 0 if not found
static int zsv_select_find_header(struct zsv_select_data *data, const unsigned char *header_name) {
  if(header_name) {
    for(unsigned int i = 0; i < data->output_cols_count; i++) {
      unsigned char *prior_header_name = zsv_select_get_header_name(data, data->out2in[i].ix);
      if(prior_header_name && !zsv_stricmp(header_name, prior_header_name))
        return i + 1;
    }
  }
  return 0;
}

static int zsv_select_add_output_col(struct zsv_select_data *data, unsigned in_ix) {
  int err = 0;
  if(data->output_cols_count < data->opts->max_columns) {
    int found = zsv_select_find_header(data, zsv_select_get_header_name(data, in_ix));
    if(data->distinct && found) {
      if(data->distinct == ZSV_SELECT_DISTINCT_MERGE) {
        // add this index
        struct zsv_select_uint_list *ix = calloc(1, sizeof(*ix));
        if(!ix)
          err = zsv_printerr(1, "Out of memory!\n");
        else {
          ix->value = in_ix;
          if(!data->out2in[found-1].merge.indexes)
            data->out2in[found-1].merge.indexes = ix;
          else
            *data->out2in[found-1].merge.last_index = ix;
          data->out2in[found-1].merge.last_index = &ix->next;
        }
      }
      return err;
    }
    if(zsv_select_excluded_current_header_name(data, in_ix))
      return err;
    data->out2in[data->output_cols_count++].ix = in_ix;
  }
  return err;
}

// not very fast, but we don't need it to be
static inline unsigned int str_array_ifind(const unsigned char *needle,
                                           unsigned char *haystack[],
                                           unsigned hay_count) {
  for(unsigned int i = 0; i < hay_count; i++) {
    if(!(needle && *needle) && !(haystack[i] && *haystack[i]))
      return i + 1;
    if(!(needle && *needle && haystack[i] && *haystack[i]))
      continue;
    if(!zsv_stricmp(needle, haystack[i]))
      return i + 1;
  }
  return 0;
}

static int zsv_select_set_output_columns(struct zsv_select_data *data) {
  int err = 0;
  unsigned int header_name_count = data->header_name_count;
  if(!data->col_argc) {
    for(unsigned int i = 0; !err && i < header_name_count; i++)
      err = zsv_select_add_output_col(data, i);
  } else if(data->use_header_indexes) {
    for(int arg_i = 0; !err && arg_i < data->col_argc; arg_i++) {
      const char *arg = data->col_argv[arg_i];
      unsigned i, j;
      switch(zsv_select_column_index_selection((const unsigned char *)arg, &i, &j)) {
      case zsv_select_column_index_selection_type_none:
        zsv_printerr(1, "Invalid column index: %s", arg);
        err = -1;
        break;
      case zsv_select_column_index_selection_type_single:
        err = zsv_select_add_output_col(data, i-1);
        break;
      case zsv_select_column_index_selection_type_range:
        while(i <= j && i < data->opts->max_columns) {
          err = zsv_select_add_output_col(data, i-1);
          i++;
        }
        break;
      case zsv_select_column_index_selection_type_lower_bounded:
        if(i) {
          for(unsigned int k = i-1; !err && k < header_name_count; k++)
            err = zsv_select_add_output_col(data, k);
        }
        break;
      }
    }
  } else { // using header names
    for(int arg_i = 0; !err && arg_i < data->col_argc; arg_i++) {
      // find the location of the matching header name, if any
      unsigned int in_pos = str_array_ifind((const unsigned char *)data->col_argv[arg_i],
                                            data->header_names, header_name_count);
      if(!in_pos) {
        fprintf(stderr, "Column %s not found\n", data->col_argv[arg_i]);
        err = -1;
      } else
        err = zsv_select_add_output_col(data, in_pos - 1);
    }
  }
  return err;
}

static void zsv_select_add_search(struct zsv_select_data *data, const char *value) {
  struct zsv_select_search_str *ss = calloc(1, sizeof(*ss));
  ss->value = value;
  ss->len = value ? strlen(value) : 0;
  ss->next = data->search_strings;
  data->search_strings = ss;
}

#ifndef NDEBUG
__attribute__((always_inline)) static inline
#endif
unsigned char *zsv_select_cell_clean(struct zsv_select_data *data, unsigned char *utf8_value, char quoted, size_t *lenp) {
  size_t len = *lenp;
  // to do: option to replace or warn non-printable chars 0 - 31:
  // vectorized scan
  // replace or warn if found

  if(UNLIKELY(!data->no_trim_whitespace))
    utf8_value = (unsigned char *)zsv_strtrim(utf8_value, &len);

  if(UNLIKELY(data->clean_white))
    len = zsv_strwhite(utf8_value, len, data->whitspace_clean_flags); // to do: zsv_clean

  if(UNLIKELY(data->embedded_lineend && quoted)) {
    unsigned char *tmp;
    const char *to_replace[] = { "\r\n", "\r", "\n" };
    for(int i = 0; i < 3; i++) {
      while((tmp = memmem(utf8_value, len, to_replace[i], strlen(to_replace[i])))) {
        if(strlen(to_replace[i]) == 1)
          *tmp = data->embedded_lineend;
        else {
          size_t right_len = utf8_value + len - tmp;
          memmove(tmp + 1, tmp + 2, right_len - 2);
          *tmp = data->embedded_lineend;
          len--;
        }
      }
    }
    if(data->no_trim_whitespace)
      utf8_value = (unsigned char *)zsv_strtrim(utf8_value, &len);
  }
  *lenp = len;
  return utf8_value;
}

static inline char zsv_select_row_search_hit(struct zsv_select_data *data, zsv_parser p) {
  if(!data->search_strings)
    return 1;

  unsigned int j = zsv_cell_count(p);
  for(unsigned int i = 0; i < j; i++) {
    struct zsv_cell cell = zsv_get_cell(p, i);
    if(UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, cell.quoted, &cell.len);
    if(cell.len) {
      for(struct zsv_select_search_str *ss = data->search_strings; ss; ss = ss->next)
        if(ss->value && *ss->value && memmem(cell.str, cell.len, ss->value, ss->len))
          return 1;
    }
  }
  return 0;
}

static enum zsv_select_column_index_selection_type
zsv_select_column_index_selection(const unsigned char *arg, unsigned *lo, unsigned *hi) {
  enum zsv_select_column_index_selection_type result = zsv_select_column_index_selection_type_none;

  unsigned int i, j, k;
  int n = 0;
  k = sscanf((const char *)arg, "%u-%u%n", &i, &j, &n);
  if(k == 2) {
    if(n == (int)strlen((const char *)arg) && i > 0 && j >= i)
      result = zsv_select_column_index_selection_type_range;
  } else {
    k = sscanf((const char *)arg, "%u%n", &i, &n);
    if(k && n == (int)strlen((const char *)arg)) {
      if(i > 0)
        result = zsv_select_column_index_selection_type_single;
    } else {
      k = sscanf((const char *)arg, "%u-%n", &i, &n);
      if(k && n == (int)strlen((const char *)arg)) {
        if(i > 0) {
          result = zsv_select_column_index_selection_type_lower_bounded;
          j = 0;
        }
      }
    }
  }
  if(lo)
    *lo = i;
  if(hi)
    *hi = j;
  return result;
}

// zsv_select_check_exclusions_are_indexes(): return err
static int zsv_select_check_exclusions_are_indexes(struct zsv_select_data *data) {
  int err = 0;
  for(unsigned int e = 0; e < data->exclusion_count; e++) {
    const unsigned char *arg = data->exclusions[e];
    if(zsv_select_column_index_selection(arg, NULL, NULL) == zsv_select_column_index_selection_type_none)
      err = zsv_printerr(1, "Invalid column index: %s", arg);
  }
  return err;
}

// demo_random_bw_1_and_100(): this is a poor random number generator. you probably
// will want to use a better one
static double demo_random_bw_1_and_100() {
#ifdef HAVE_ARC4RANDOM_UNIFORM
  return (long double)(arc4random_uniform(1000000)) / 10000;
#else
  double max = 100.0;
  unsigned int n;
# ifdef HAVE_RAND_S
  unsigned int tries = 0;
  while(rand_s(&n) && tries++ < 10);
  return (double) n / ((double) UINT_MAX + 1) * max;
# else
  unsigned int umax = ~0;
  n = rand();
  return (double) n / ((double) (umax) + 1) * max;
# endif
#endif
}

// zsv_select_output_row(): output row data
static void zsv_select_output_data_row(struct zsv_select_data *data, zsv_parser p) {
  unsigned int cnt = data->output_cols_count;
  char first = 1;
  if(data->prepend_line_number) {
    zsv_writer_cell_zu(data->csv_writer, first, data->data_row_count);
    first = 0;
  }

  /* print data row */
  for(unsigned int i = 0; i < cnt; i++) { // for each output column
    unsigned int in_ix = data->out2in[i].ix;
    struct zsv_cell cell = zsv_get_cell(p, in_ix);
    if(UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, cell.quoted, &cell.len);
    if(VERY_UNLIKELY(data->distinct == ZSV_SELECT_DISTINCT_MERGE)) {
      if(UNLIKELY(cell.len == 0)) {
        for(struct zsv_select_uint_list *ix = data->out2in[i].merge.indexes; ix; ix = ix->next) {
          unsigned int m_ix = ix->value;
          cell = zsv_get_cell(p, m_ix);
          if(cell.len) {
            if(UNLIKELY(data->any_clean != 0))
              cell.str = zsv_select_cell_clean(data, cell.str, cell.quoted, &cell.len);
            if(cell.len)
              break;
          }
        }
      }
    }
    zsv_writer_cell(data->csv_writer, first, cell.str, cell.len, cell.quoted);
    first = 0;
  }
}

static void zsv_select_data_row(struct zsv_select_data *data, zsv_parser p) {
  data->data_row_count++;

  if(UNLIKELY(zsv_cell_count(p) == 0 || data->cancelled))
    return;

  // check if we should skip this row
  data->skip_this_row = 0;
  if(UNLIKELY(data->skip_data_rows)) {
    data->skip_data_rows--;
    data->skip_this_row = 1;
  } else if(UNLIKELY(data->sample_every_n || data->sample_pct)) {
    data->skip_this_row = 1;
    if(data->sample_every_n && data->data_row_count % data->sample_every_n == 1)
      data->skip_this_row = 0;
    if(data->sample_pct && demo_random_bw_1_and_100() <= data->sample_pct)
      data->skip_this_row = 0;
  }

  if(LIKELY(!data->skip_this_row)) {
    // if we have a search filter, check that
    char skip = 0;
    skip = !zsv_select_row_search_hit(data, p);
    if(!skip) {

      // print the data row
      zsv_select_output_data_row(data, p);
      if(UNLIKELY(data->data_rows_limit > 0))
        if(data->data_row_count + 1 >= data->data_rows_limit)
          data->cancelled = 1;
    }
  }
  if(data->data_row_count % 25000 == 0 && data->verbose)
    fprintf(stderr, "Processed %zu rows\n", data->data_row_count);
}

static void zsv_select_print_header_row(struct zsv_select_data *data) {
  if(data->no_header)
    return;
  zsv_writer_cell_prepend(data->csv_writer, (const unsigned char *)data->prepend_header);
  if(data->prepend_line_number)
    zsv_writer_cell_s(data->csv_writer, 1, (const unsigned char *)"#", 0);
  for(unsigned int i = 0; i < data->output_cols_count; i++) {
    unsigned char *header_name = zsv_select_get_header_name(data, data->out2in[i].ix);
    zsv_writer_cell_s(data->csv_writer, i == 0 && !data->prepend_line_number, header_name, 1);
  }
  zsv_writer_cell_prepend(data->csv_writer, NULL);
}

static void zsv_select_header_finish(struct zsv_select_data *data) {
  if(zsv_select_set_output_columns(data))
    data->cancelled = 1;
  else
    zsv_select_print_header_row(data);
}

static void zsv_select_header_row(struct zsv_select_data *data, zsv_parser p) {
  if(data->cancelled)
    return;

  unsigned int cols = zsv_cell_count(p);
  unsigned int max_header_ix = 0;
  for(unsigned int i = 0; i < cols; i++) {
    struct zsv_cell cell = zsv_get_cell(p, i);
    if(UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, cell.quoted, &cell.len);
    if(i < data->opts->max_columns) {
      data->header_names[i] = zsv_memdup(cell.str, cell.len);
      max_header_ix = i+1;
    }
  }

  // in case we want to make this an option later
  char trim_trailing_columns = 1;
  if(!trim_trailing_columns)
    max_header_ix = cols;

  if(max_header_ix > data->header_name_count)
    data->header_name_count = max_header_ix;

  zsv_select_header_finish(data);
}

#define ZSV_SELECT_MAX_COLS_DEFAULT 1024
#define ZSV_SELECT_MAX_COLS_DEFAULT_S "1024"

const char *zsv_select_usage_msg[] = {
  APPNAME ": streaming CSV parser",
  "",
  "Usage: " APPNAME " [filename] [options] [-- col_specifier [... col_specifier]]",
  "  where col_specifier is a column name or, if the -n option is used,",
  "   a column index (starting at 1) or index range in the form of n-m",
  "  e.g. " APPNAME " -n myfile.csv -- 1 4-6 50 10",
  "       " APPNAME " myfile.csv -- first_col fiftieth_column \"Tenth Column\"",
  "",
  "Extracts and outputs specified columns. Outputs the input columns that are specified after",
  "the '--' separator, or all columns if no '--' separator is provided",
  "",
  "Options:",
  "  -b, --with-bom : output with BOM",
  /*
  "  --fixed <offset1,offset2,offset3>: parse as fixed-width text; use given comma-separated list of positive integers for cell end indexes",
  */
#ifndef ZSV_CLI
  "  -v, --verbose: verbose output",
#endif
  "  -H,--head <n>               : (head) only process the first n rows of data from all rows (including header) in the input",
  "  --no-header                 : do not output a header row",
  "  --prepend-header <value>    : prepend each column header with the given text value",
  "  -s, --search <value>: only output rows with at least one cell containing value",
  // to do: " -s, --search /<pattern>/modifiers: search on regex pattern; modifiers include 'g' (global) and 'i' (case-insensitive)",
  "  --sample-every <num of rows>: output a sample consisting of the first row, then every nth row",
  "  --sample-pct   <percentage>: output a randomly-selected sample (32 bits of randomness) of n percent of the input rows",
  "  -d, --header-row-span <n>: apply header depth (rowspan) of n",
  "  --distinct: skip subsequent occurrences of columns with the same name",
  "  --merge: merge subsequent occurrences of columns with the same name, outputting first non-null value",
  // --rename: like distinct, but instead of removing cols with dupe names, renames them, trying _<n> for n up to max cols
  "  -e <embedded lineend char>: char to replace embedded lineend. if none provided, embedded lineends are preserved",
  "      If the provided string begins with 0x, it will be interpreted as the hex representation of a string",
  "  -x <column>: exclude the indicated column. can be specified more than once",
  "  -N, --line-number: prefix each row with the row number",
  "  -n: provided column indexes are numbers corresponding to column positions (starting with 1), instead of names",
#ifndef ZSV_CLI
  "  -T: input is tab-delimited, instead of comma-delimited",
  "  -O, --other-delim <delim>: input is delimited with the given char, instead of comma-delimited",
  "                             Note: this option does not support quoted values with embedded delimiters",
#endif
  "  -w, --whitespace-clean: normalize all whitespace to space or newline, single-char (non-consecutive) occurrences",
  "  --whitespace-clean-no-newline: clean whitespace and remove embedded newlines",
  "  -W, --no-trim: do not trim whitespace",
#ifndef ZSV_CLI
  "  -C <maximum_number_of_columns>: defaults to " ZSV_SELECT_MAX_COLS_DEFAULT_S,
  "  -L, --max-row-size <n>: set the maximum memory used for a single row",
  "                          defaults to " ZSV_ROW_MAX_SIZE_DEFAULT_S ", min " ZSV_ROW_MAX_SIZE_MIN_S ")",
#endif
  "  -o <output filename>: name of file to save output to",
  NULL
};

static void zsv_select_usage() {
  for(int i = 0; zsv_select_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_select_usage_msg[i]);
}

static void zsv_select_cleanup(struct zsv_select_data *data) {
  if(data->opts->stream && data->opts->stream != stdin)
    fclose(data->opts->stream);

  zsv_writer_delete(data->csv_writer);
  zsv_select_search_str_delete(data->search_strings);

  if(data->distinct == ZSV_SELECT_DISTINCT_MERGE) {
    for(unsigned int i = 0; i < data->output_cols_count; i++) {
      for(struct zsv_select_uint_list *next, *ix = data->out2in[i].merge.indexes; ix; ix = next) {
        next = ix->next;
        free(ix);
      }
    }
  }
  free(data->out2in);

  for(unsigned int i = 0; i < data->header_name_count; i++)
    free(data->header_names[i]);
  free(data->header_names);

/*  free(data->fixed.offsets); */
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsv_select_usage();
    return zsv_status_ok;
  }

  struct zsv_select_data data = { 0 };
  data.opts = opts;
  const char *input_path = NULL;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  int col_index_arg_i = 0;
  enum zsv_status stat = zsv_status_ok;
  for(int arg_i = 1; stat == zsv_status_ok && arg_i < argc; arg_i++) {
    if(!strcmp(argv[arg_i], "--")) {
      col_index_arg_i = arg_i + 1;
      break;
    }
    if(!strcmp(argv[arg_i], "-b") || !strcmp(argv[arg_i], "--with-bom"))
      writer_opts.with_bom = 1;
    /*
    else if(!strcmp(argv[arg_i], "--fixed")) {
      if(++arg_i >= argc)
        stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i-1]);
      else { // parse offsets
        data.fixed.count = 1;
        for(const char *s = argv[arg_i]; *s; s++)
          if(*s == ',')
            data.fixed.count++;
        free(data.fixed.offsets);
        data.fixed.offsets = malloc(data.fixed.count * sizeof(*data.fixed.offsets));
        size_t count = 0;
        const char *start = argv[arg_i];
        for(const char *end = argv[arg_i]; ; end++) {
          if(*end == ',' || *end == '\0') {
            if(!sscanf(start, "%zu,", &data.fixed.offsets[count++])) {
              stat = zsv_printerr(1, "Invalid offset: %s.*\n", end - start, start);
              break;
            } else if(*end == '\0')
              break;
            else {
              start = end + 1;
              if(*start == '\0')
                break;
            }
          }
        }
      }
    } */
    else if(!strcmp(argv[arg_i], "--distinct"))
      data.distinct = 1;
    else if(!strcmp(argv[arg_i], "--merge"))
      data.distinct = ZSV_SELECT_DISTINCT_MERGE;
    else if(!strcmp(argv[arg_i], "-o") || !strcmp(argv[arg_i], "--output")) {
      if(++arg_i >= argc)
        stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i-1]);
      else if(writer_opts.stream && writer_opts.stream != stdout)
        stat = zsv_printerr(1, "Output file specified more than once");
      else if(!(writer_opts.stream = fopen(argv[arg_i], "wb")))
        stat = zsv_printerr(1, "Unable to open for writing: %s", argv[arg_i]);
      else if(data.opts->verbose)
        fprintf(stderr, "Opened %s for write\n", argv[arg_i]);
    } else if(!strcmp(argv[arg_i], "-N") || !strcmp(argv[arg_i], "--line-number")) {
      data.prepend_line_number = 1;
    } else if(!strcmp(argv[arg_i], "-n"))
      data.use_header_indexes = 1;
    else if(!strcmp(argv[arg_i], "-s") || !strcmp(argv[arg_i], "--search")) {
      arg_i++;
      if(arg_i < argc && strlen(argv[arg_i]))
        zsv_select_add_search(&data, argv[arg_i]);
      else
        stat = zsv_printerr(1, "%s option requires a value", argv[arg_i-1]);
    } else if(!strcmp(argv[arg_i], "-v") || !strcmp(argv[arg_i], "--verbose")) {
      data.verbose = 1;
    } else if(!strcmp(argv[arg_i], "-w") || !strcmp(argv[arg_i], "--whitespace-clean"))
      data.clean_white = 1;
    else if(!strcmp(argv[arg_i], "--whitespace-clean-no-newline")) {
      data.clean_white = 1;
      data.whitspace_clean_flags = 1;
    } else if(!strcmp(argv[arg_i], "-W") || !strcmp(argv[arg_i], "--no-trim")) {
      data.no_trim_whitespace = 1;
    } else if(!strcmp(argv[arg_i], "--sample-every")) {
      arg_i++;
      if(!(arg_i < argc))
        stat = zsv_printerr(1, "--sample-every option requires a value");
      else if(atoi(argv[arg_i]) <= 0)
        stat = zsv_printerr(1, "--sample-every value should be an integer > 0");
      else
        data.sample_every_n = atoi(argv[arg_i]);
    } else if(!strcmp(argv[arg_i], "--sample-pct")) {
      arg_i++;
      double d;
      if(!(arg_i < argc))
        stat = zsv_printerr(1, "--sample-pct option requires a value");
      else if(!(d = atof(argv[arg_i])) && d > 0 && d < 100)
        stat = zsv_printerr(-1, "--sample-pct value should be a number between 0 and 100 (e.g. 1.5 for a sample of 1.5% of the data");
      else
        data.sample_pct = d;
    } else if(!strcmp(argv[arg_i], "--prepend-header")) {
      if(!(arg_i + 1 < argc))
        stat = zsv_printerr(1, "%s option requires a value");
      else
        data.prepend_header = argv[++arg_i];
    } else if(!strcmp(argv[arg_i], "--no-header")) {
      data.no_header = 1;
    } else if(!strcmp(argv[arg_i], "-H") || !strcmp(argv[arg_i], "--head")) {
      if(!(arg_i + 1 < argc && atoi(argv[arg_i+1]) >= 0))
        stat = zsv_printerr(1, "%s option value invalid: should be positive integer; got %s", argv[arg_i], arg_i + 1 < argc ? argv[arg_i+1] : "");
      else
        data.data_rows_limit = atoi(argv[++arg_i]) + 1;
    } else if(!strcmp(argv[arg_i], "-D") || !strcmp(argv[arg_i], "--skip-data")) {
      ++arg_i;
      if(!(arg_i < argc && atoi(argv[arg_i]) >= 0))
        stat = zsv_printerr(1, "%s option value invalid: should be positive integer", argv[arg_i-1]);
      else
        data.skip_data_rows = atoi(argv[arg_i]);
    } else if(!strcmp(argv[arg_i], "-e")) {
      ++arg_i;
      if(data.embedded_lineend)
        stat = zsv_printerr(1, "-e option specified more than once");
      else if(strlen(argv[arg_i]) != 1)
        stat = zsv_printerr(1, "-e option value must be a single character");
      else if(arg_i < argc)
        data.embedded_lineend = *argv[arg_i];
      else
        stat = zsv_printerr(1, "-e option requires a value");
    } else if(!strcmp(argv[arg_i], "-x")) {
      arg_i++;
      if(!(arg_i < argc))
        stat = zsv_printerr(1, "%s option requires a value", argv[arg_i-1]);
      else
        zsv_select_add_exclusion(&data, argv[arg_i]);
    } else if(*argv[arg_i] == '-')
      stat = zsv_printerr(1, "Unrecognized argument: %s", argv[arg_i]);
    else if(data.opts->stream)
      stat = zsv_printerr(1, "Input file was specified, cannot also read: %s", argv[arg_i]);
    else if(!(data.opts->stream = fopen(argv[arg_i], "rb")))
      stat = zsv_printerr(1, "Could not open for reading: %s", argv[arg_i]);
    else
      input_path = argv[arg_i];
  }

  if(data.sample_pct)
    srand(time(0));

  if(data.use_header_indexes && stat == zsv_status_ok)
    stat = zsv_select_check_exclusions_are_indexes(&data);

  if(!data.opts->stream) {
#ifdef NO_STDIN
    stat = zsv_printerr(1, "Please specify an input file");
#else
    data.opts->stream = stdin;
#endif
  }

  if(stat == zsv_status_ok) {
    if(!col_index_arg_i)
      data.col_argc = 0;
    else {
      data.col_argv = &argv[col_index_arg_i];
      data.col_argc = argc - col_index_arg_i;
    }

    data.header_names = calloc(data.opts->max_columns, sizeof(*data.header_names));
    assert(data.opts->max_columns > 0);
    data.out2in = calloc(data.opts->max_columns, sizeof(*data.out2in));
    data.csv_writer = zsv_writer_new(&writer_opts);
    if(!(data.header_names && data.csv_writer))
      stat = zsv_status_memory;
    else {
      zsv_parser parser;
      if(zsv_new_with_properties(data.opts, custom_prop_handler, input_path, opts_used, &parser)
         == zsv_status_ok) {
        // all done with
        data.any_clean =
          !data.no_trim_whitespace
          || data.clean_white
          || data.embedded_lineend;

        // to do: support fixed input
        /*
        if(data.fixed.count && zsv_set_fixed_offsets(parser, data.fixed.count,
                                                     data.fixed.offsets)
           != zsv_status_ok)
          data.cancelled = 1;
          */

        // create a local csv writer buff quoted values
        unsigned char writer_buff[512];
        zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

        // process the input data
        zsv_handle_ctrl_c_signal();
        enum zsv_status status = zsv_next_row(parser);
        if(status == zsv_status_row)
          zsv_select_header_row(&data, parser);
        while((status = zsv_next_row(parser)) == zsv_status_row)
          zsv_select_data_row(&data, parser);
        zsv_delete(parser);
      }
    }
  }
  zsv_select_cleanup(&data);
  if(writer_opts.stream && writer_opts.stream != stdout)
    fclose(writer_opts.stream);
  return stat;
}
