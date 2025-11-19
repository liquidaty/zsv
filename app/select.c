/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <assert.h>
#ifdef _WIN32
#define _CRT_RAND_S /* for random number generator, used when sampling. must come before including stdlib.h */
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

#define ZSV_COMMAND select
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/string.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/memmem.h>
#include <zsv/utils/arg.h>

#include "select/internal.h" // various defines and structs
#include "select/usage.c"    // zsv_select_usage_msg
#include "select/rand.c"     // demo_random_bw_1_and_100()
#include "select/fixed.c"    // auto_detect_fixed_column_sizes()

// zsv_select_add_search(), zsv_select_search_str_delete()
#include "select/search.c"

// struct zsv_select_regex, zsv_select_add_regex(), zsv_select_regexs_delete()
#include "select/regex.c"

// zsv_select_cell_clean(), zsv_select_row_search_hit()
#include "select/processing.c"

// zsv_select_add_exclusion(), zsv_select_get_header_name(),
// zsv_select_check_exclusions_are_indexes()
#include "select/selection.c"

// zsv_select_output_data_row(): output row data
static void zsv_select_output_data_row(struct zsv_select_data *data) {
  unsigned int cnt = data->output_cols_count;
  char first = 1;
  if (data->prepend_line_number) {
    zsv_writer_cell_zu(data->csv_writer, first, data->data_row_count);
    first = 0;
  }

  /* print data row */
  for (unsigned int i = 0; i < cnt; i++) { // for each output column
    unsigned int in_ix = data->out2in[i].ix;
    struct zsv_cell cell = zsv_get_cell(data->parser, in_ix);
    if (UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
    if (VERY_UNLIKELY(data->distinct == ZSV_SELECT_DISTINCT_MERGE)) {
      if (UNLIKELY(cell.len == 0)) {
        for (struct zsv_select_uint_list *ix = data->out2in[i].merge.indexes; ix; ix = ix->next) {
          unsigned int m_ix = ix->value;
          cell = zsv_get_cell(data->parser, m_ix);
          if (cell.len) {
            if (UNLIKELY(data->any_clean != 0))
              cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
            if (cell.len)
              break;
          }
        }
      }
    }
    zsv_writer_cell(data->csv_writer, first, cell.str, cell.len, cell.quoted);
    first = 0;
  }
}

static void zsv_select_data_row(void *ctx) {
  struct zsv_select_data *data = ctx;
  data->data_row_count++;

  if (UNLIKELY(zsv_cell_count(data->parser) == 0 || data->cancelled))
    return;

  // check if we should skip this row
  data->skip_this_row = 0;
  if (UNLIKELY(data->skip_data_rows)) {
    data->skip_data_rows--;
    data->skip_this_row = 1;
  } else if (UNLIKELY(data->sample_every_n || data->sample_pct)) {
    data->skip_this_row = 1;
    if (data->sample_every_n && data->data_row_count % data->sample_every_n == 1)
      data->skip_this_row = 0;
    if (data->sample_pct && demo_random_bw_1_and_100() <= data->sample_pct)
      data->skip_this_row = 0;
  }

  if (LIKELY(!data->skip_this_row)) {
    // if we have a search filter, check that
    char skip = 0;
    skip = !zsv_select_row_search_hit(data);
    if (!skip) {

      // print the data row
      zsv_select_output_data_row(data);
      if (UNLIKELY(data->data_rows_limit > 0))
        if (data->data_row_count + 1 >= data->data_rows_limit)
          data->cancelled = 1;
    }
  }
  if (data->data_row_count % 25000 == 0 && data->verbose)
    fprintf(stderr, "Processed %zu rows\n", data->data_row_count);
}

static void zsv_select_print_header_row(struct zsv_select_data *data) {
  if (data->no_header)
    return;
  zsv_writer_cell_prepend(data->csv_writer, (const unsigned char *)data->prepend_header);
  if (data->prepend_line_number)
    zsv_writer_cell_s(data->csv_writer, 1, (const unsigned char *)"#", 0);
  for (unsigned int i = 0; i < data->output_cols_count; i++) {
    unsigned char *header_name = zsv_select_get_header_name(data, data->out2in[i].ix);
    zsv_writer_cell_s(data->csv_writer, i == 0 && !data->prepend_line_number, header_name, 1);
  }
  zsv_writer_cell_prepend(data->csv_writer, NULL);
}

static void zsv_select_header_finish(struct zsv_select_data *data) {
  if (zsv_select_set_output_columns(data))
    data->cancelled = 1;
  else {
    zsv_select_print_header_row(data);
    zsv_set_row_handler(data->parser, zsv_select_data_row);
  }
}

static void zsv_select_header_row(void *ctx) {
  struct zsv_select_data *data = ctx;

  if (data->cancelled)
    return;

  unsigned int cols = zsv_cell_count(data->parser);
  unsigned int max_header_ix = 0;
  for (unsigned int i = 0; i < cols; i++) {
    struct zsv_cell cell = zsv_get_cell(data->parser, i);
    if (UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
    if (i < data->opts->max_columns) {
      data->header_names[i] = zsv_memdup(cell.str, cell.len);
      max_header_ix = i + 1;
    }
  }

  // in case we want to make this an option later
  char trim_trailing_columns = 1;
  if (!trim_trailing_columns)
    max_header_ix = cols;

  if (max_header_ix > data->header_name_count)
    data->header_name_count = max_header_ix;

  zsv_select_header_finish(data);
}

static void zsv_select_usage(void) {
  for (size_t i = 0; zsv_select_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_select_usage_msg[i]);
}

static void zsv_select_cleanup(struct zsv_select_data *data) {
  if (data->opts->stream && data->opts->stream != stdin)
    fclose(data->opts->stream);

  zsv_writer_delete(data->csv_writer);
  zsv_select_search_str_delete(data->search_strings);
#ifdef HAVE_PCRE2_8
  zsv_select_regexs_delete(data->search_regexs);
#endif

  if (data->distinct == ZSV_SELECT_DISTINCT_MERGE) {
    for (unsigned int i = 0; i < data->output_cols_count; i++) {
      for (struct zsv_select_uint_list *next, *ix = data->out2in[i].merge.indexes; ix; ix = next) {
        next = ix->next;
        free(ix);
      }
    }
  }
  free(data->out2in);

  for (unsigned int i = 0; i < data->header_name_count; i++)
    free(data->header_names[i]);
  free(data->header_names);

  free(data->fixed.offsets);
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsv_select_usage();
    return zsv_status_ok;
  }

  char fixed_auto = 0;
  struct zsv_select_data data = {0};
  data.opts = opts;
  const char *input_path = NULL;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  int col_index_arg_i = 0;
  unsigned char *preview_buff = NULL;
  size_t preview_buff_len = 0;

  enum zsv_status stat = zsv_status_ok;
  for (int arg_i = 1; stat == zsv_status_ok && arg_i < argc; arg_i++) {
    if (!strcmp(argv[arg_i], "--")) {
      col_index_arg_i = arg_i + 1;
      break;
    }
    if (!strcmp(argv[arg_i], "-b") || !strcmp(argv[arg_i], "--with-bom"))
      writer_opts.with_bom = 1;
    else if (!strcmp(argv[arg_i], "--fixed-auto-max-lines")) {
      if (++arg_i < argc && atoi(argv[arg_i]) > 0)
        data.fixed.max_lines = (size_t)atoi(argv[arg_i]);
      else
        stat = zsv_printerr(1, "%s option requires value > 0", argv[arg_i - 1]);
      ;
    } else if (!strcmp(argv[arg_i], "--fixed-auto"))
      fixed_auto = 1;
    else if (!strcmp(argv[arg_i], "--fixed")) {
      if (++arg_i >= argc)
        stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i - 1]);
      else { // parse offsets
        data.fixed.count = 1;
        for (const char *s = argv[arg_i]; *s; s++)
          if (*s == ',')
            data.fixed.count++;
        free(data.fixed.offsets);
        data.fixed.offsets = calloc(data.fixed.count, sizeof(*data.fixed.offsets));
        if (data.fixed.offsets == NULL) {
          stat = zsv_printerr(1, "Out of memory!\n");
          break;
        }
        size_t count = 0;
        const char *start = argv[arg_i];
        for (const char *end = argv[arg_i];; end++) {
          if (*end == ',' || *end == '\0') {
            if (sscanf(start, "%zu,", &data.fixed.offsets[count++]) != 1) {
              stat = zsv_printerr(1, "Invalid offset: %.*s\n", end - start, start);
              break;
            } else if (*end == '\0')
              break;
            else {
              start = end + 1;
              if (*start == '\0')
                break;
            }
          }
        }
      }
    } else if (!strcmp(argv[arg_i], "--distinct"))
      data.distinct = 1;
    else if (!strcmp(argv[arg_i], "--merge"))
      data.distinct = ZSV_SELECT_DISTINCT_MERGE;
    else if (!strcmp(argv[arg_i], "-o") || !strcmp(argv[arg_i], "--output")) {
      if (++arg_i >= argc)
        stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i - 1]);
      else if (writer_opts.stream && writer_opts.stream != stdout)
        stat = zsv_printerr(1, "Output file specified more than once");
      else if (!(writer_opts.stream = fopen(argv[arg_i], "wb")))
        stat = zsv_printerr(1, "Unable to open for writing: %s", argv[arg_i]);
      else if (data.opts->verbose)
        fprintf(stderr, "Opened %s for write\n", argv[arg_i]);
    } else if (!strcmp(argv[arg_i], "-N") || !strcmp(argv[arg_i], "--line-number")) {
      data.prepend_line_number = 1;
    } else if (!strcmp(argv[arg_i], "-n"))
      data.use_header_indexes = 1;
    else if (!strcmp(argv[arg_i], "-s") || !strcmp(argv[arg_i], "--search")) {
      arg_i++;
      if (arg_i < argc && strlen(argv[arg_i]))
        zsv_select_add_search(&data, argv[arg_i]);
      else
        stat = zsv_printerr(1, "%s option requires a value", argv[arg_i - 1]);
#ifdef HAVE_PCRE2_8
    } else if (!strcmp(argv[arg_i], "--regex-search")) {
      arg_i++;
      if (arg_i < argc && strlen(argv[arg_i]))
        zsv_select_add_regex(&data, argv[arg_i]);
      else
        stat = zsv_printerr(1, "%s option requires a value", argv[arg_i - 1]);
#endif
    } else if (!strcmp(argv[arg_i], "-v") || !strcmp(argv[arg_i], "--verbose")) {
      data.verbose = 1;
    } else if (!strcmp(argv[arg_i], "--unescape")) {
      data.unescape = 1;
    } else if (!strcmp(argv[arg_i], "-w") || !strcmp(argv[arg_i], "--whitespace-clean"))
      data.clean_white = 1;
    else if (!strcmp(argv[arg_i], "--whitespace-clean-no-newline")) {
      data.clean_white = 1;
      data.whitespace_clean_flags = 1;
    } else if (!strcmp(argv[arg_i], "-W") || !strcmp(argv[arg_i], "--no-trim")) {
      data.no_trim_whitespace = 1;
    } else if (!strcmp(argv[arg_i], "--sample-every")) {
      arg_i++;
      if (!(arg_i < argc))
        stat = zsv_printerr(1, "--sample-every option requires a value");
      else if (atoi(argv[arg_i]) <= 0)
        stat = zsv_printerr(1, "--sample-every value should be an integer > 0");
      else
        data.sample_every_n = atoi(argv[arg_i]); // TO DO: check for overflow
    } else if (!strcmp(argv[arg_i], "--sample-pct")) {
      arg_i++;
      double d;
      if (!(arg_i < argc))
        stat = zsv_printerr(1, "--sample-pct option requires a value");
      else if (!(d = atof(argv[arg_i])) && d > 0 && d < 100)
        stat = zsv_printerr(
          -1, "--sample-pct value should be a number between 0 and 100 (e.g. 1.5 for a sample of 1.5%% of the data");
      else
        data.sample_pct = d;
    } else if (!strcmp(argv[arg_i], "--prepend-header")) {
      int err1 = 0;
      data.prepend_header = zsv_next_arg(++arg_i, argc, argv, &err1);
      if (err1)
        stat = zsv_status_error;
    } else if (!strcmp(argv[arg_i], "--no-header"))
      data.no_header = 1;
    else if (!strcmp(argv[arg_i], "-H") || !strcmp(argv[arg_i], "--head")) {
      if (!(arg_i + 1 < argc && atoi(argv[arg_i + 1]) >= 0))
        stat = zsv_printerr(1, "%s option value invalid: should be positive integer; got %s", argv[arg_i],
                            arg_i + 1 < argc ? argv[arg_i + 1] : "");
      else
        data.data_rows_limit = atoi(argv[++arg_i]) + 1;
    } else if (!strcmp(argv[arg_i], "-D") || !strcmp(argv[arg_i], "--skip-data")) {
      ++arg_i;
      if (!(arg_i < argc && atoi(argv[arg_i]) >= 0))
        stat = zsv_printerr(1, "%s option value invalid: should be positive integer", argv[arg_i - 1]);
      else
        data.skip_data_rows = atoi(argv[arg_i]);
    } else if (!strcmp(argv[arg_i], "-e")) {
      ++arg_i;
      if (data.embedded_lineend)
        stat = zsv_printerr(1, "-e option specified more than once");
      else if (strlen(argv[arg_i]) != 1)
        stat = zsv_printerr(1, "-e option value must be a single character");
      else if (arg_i < argc)
        data.embedded_lineend = *argv[arg_i];
      else
        stat = zsv_printerr(1, "-e option requires a value");
    } else if (!strcmp(argv[arg_i], "-x")) {
      arg_i++;
      if (!(arg_i < argc))
        stat = zsv_printerr(1, "%s option requires a value", argv[arg_i - 1]);
      else
        zsv_select_add_exclusion(&data, argv[arg_i]);
    } else if (*argv[arg_i] == '-')
      stat = zsv_printerr(1, "Unrecognized argument: %s", argv[arg_i]);
    else if (data.opts->stream)
      stat = zsv_printerr(1, "Input file was specified, cannot also read: %s", argv[arg_i]);
    else if (!(data.opts->stream = fopen(argv[arg_i], "rb")))
      stat = zsv_printerr(1, "Could not open for reading: %s", argv[arg_i]);
    else
      input_path = argv[arg_i];
  }

  if (stat == zsv_status_ok) {
    if (data.sample_pct)
      srand(time(0));

    if (data.use_header_indexes && stat == zsv_status_ok)
      stat = zsv_select_check_exclusions_are_indexes(&data);
  }

  if (stat == zsv_status_ok) {
    if (!data.opts->stream) {
#ifdef NO_STDIN
      stat = zsv_printerr(1, "Please specify an input file");
#else
      data.opts->stream = stdin;
#endif
    }

    if (stat == zsv_status_ok && fixed_auto) {
      if (data.fixed.offsets)
        stat = zsv_printerr(zsv_status_error, "Please specify either --fixed-auto or --fixed, but not both");
      else if (data.opts->insert_header_row)
        stat = zsv_printerr(zsv_status_error, "--fixed-auto can not be specified together with --header-row");
      else {
        size_t buffsize = 1024 * 256; // read the first
        preview_buff = calloc(buffsize, sizeof(*preview_buff));
        if (!preview_buff)
          stat = zsv_printerr(zsv_status_memory, "Out of memory!");
        else
          stat = auto_detect_fixed_column_sizes(&data.fixed, data.opts, preview_buff, buffsize, &preview_buff_len,
                                                opts->verbose);
      }
    }
  }

  if (stat == zsv_status_ok) {
    if (!col_index_arg_i)
      data.col_argc = 0;
    else {
      data.col_argv = &argv[col_index_arg_i];
      data.col_argc = argc - col_index_arg_i;
    }

    data.header_names = calloc(data.opts->max_columns, sizeof(*data.header_names));
    assert(data.opts->max_columns > 0);
    data.out2in = calloc(data.opts->max_columns, sizeof(*data.out2in));
    data.csv_writer = zsv_writer_new(&writer_opts);
    if (!(data.header_names && data.csv_writer))
      stat = zsv_status_memory;
    else {
      data.opts->row_handler = zsv_select_header_row;
      data.opts->ctx = &data;
      if (zsv_new_with_properties(data.opts, custom_prop_handler, input_path, &data.parser) == zsv_status_ok) {
        // all done with
        data.any_clean = !data.no_trim_whitespace || data.clean_white || data.embedded_lineend || data.unescape;
        ;

        // set to fixed if applicable
        if (data.fixed.count &&
            zsv_set_fixed_offsets(data.parser, data.fixed.count, data.fixed.offsets) != zsv_status_ok)
          data.cancelled = 1;

        // create a local csv writer buff quoted values
        unsigned char writer_buff[512];
        zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

        // process the input data
        zsv_handle_ctrl_c_signal();
        enum zsv_status status = zsv_status_ok;
        if (preview_buff && preview_buff_len)
          status = zsv_parse_bytes(data.parser, preview_buff, preview_buff_len);

        while (status == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
          status = zsv_parse_more(data.parser);
        if (status == zsv_status_no_more_input)
          status = zsv_finish(data.parser);
        zsv_delete(data.parser);
      }
    }
  }
  free(preview_buff);
  zsv_select_cleanup(&data);
  if (writer_opts.stream && writer_opts.stream != stdout)
    fclose(writer_opts.stream);
  return stat;
}
