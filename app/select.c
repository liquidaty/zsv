/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

// proof of concept for parallelization
// - to do: change temp output file to a) use tmpfile, and-- maybe if improves performnace-- b) use memory first before
// falling back to file?
// - disallow parallelization when certain options are enabled:
//    - max_rows
//    - overwrite_auto
//    - overwrite
// - after processing chunk N, once start is verified, verify starting position of chunk N+1 and handle error:
//   - exit with error, and/or
//   - reprocess N+1

#include <stdio.h>
#include <assert.h>
#ifdef _WIN32
#define _CRT_RAND_S /* for random number generator, used when sampling. must come before including stdlib.h */
#else
#include <sys/types.h> // for off_t
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

// Added for pthreads and parallel I/O management
#include <pthread.h>
#include <string.h> // For memcpy, free, etc.

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
#include "utils/zsv_chunk.c"
#include "utils/cat.c"

// zsv_select_add_search(), zsv_select_search_str_delete()
#include "select/search.c"

// struct zsv_select_regex, zsv_select_add_regex(), zsv_select_regexs_delete()
#include "select/regex.c"

// zsv_select_cell_clean(), zsv_select_row_search_hit()
#include "select/processing.c"

// zsv_select_add_exclusion(), zsv_select_get_header_name(),
// zsv_select_check_exclusions_are_indexes()
#include "select/selection.c"

#ifndef NO_PARALLEL
#include "select/parallel.c"
// TO DO: make PARALLEL_THRESHOLD_BYTES customizable
#define PARALLEL_THRESHOLD_BYTES (10 * 1024 * 1024)
#endif

static void zsv_select_data_row(void *ctx);
static void zsv_select_data_row_parallel(void *ctx);

void *zsv_process_chunk(void *arg) {
  struct zsv_chunk_data *cdata = (struct zsv_chunk_data *)arg;
  struct zsv_select_data data = {0}; // Local, non-shared zsv_select_data instance

  // Copy necessary setup data from the global context
  memcpy(&data, cdata->opts->ctx, sizeof(data));

  struct zsv_opts opts = {0};
  opts.max_columns = cdata->opts->max_columns;
  opts.max_row_size = cdata->opts->max_row_size;
  opts.delimiter = cdata->opts->delimiter;
  opts.no_quotes = cdata->opts->no_quotes;
  opts.verbose = cdata->opts->verbose;
  opts.malformed_utf8_replace = cdata->opts->malformed_utf8_replace;
  opts.errprintf = cdata->opts->errprintf;
  opts.errf = cdata->opts->errf;
  opts.errclose = cdata->opts->errclose;
  opts.progress = cdata->opts->progress;

  data.parallel_data = NULL; // Clear parallel data pointer in local copy

  // 1. Setup Input Stream for the Chunk
  FILE *stream = fopen(data.input_path, "rb");
  if (!stream) {
    cdata->status = zsv_status_error;
    return NULL;
  }
  // Seek to the start of the chunk
  fseeko(stream, cdata->start_offset, SEEK_SET);
  fprintf(stderr, "chunk %i seek'd to %zu, will process up to %zu\n", cdata->id, cdata->start_offset,
          cdata->end_offset);

  // 2. Setup Temporary Output Buffer (open_memstream for private buffering)
  struct zsv_csv_writer_options writer_opts = {0};

  asprintf(&cdata->tmp_output_filename, "/tmp/select_chunk_%03d.csv", cdata->id);
  writer_opts.stream = fopen(cdata->tmp_output_filename, "wb");

  if (!writer_opts.stream) {
    cdata->status = zsv_status_memory;
    fclose(stream);
    return NULL;
  }
  data.csv_writer = zsv_writer_new(&writer_opts);

  // 3. Initialize Parser for Chunk
  opts.stream = stream;
  opts.row_handler = zsv_select_data_row_parallel; // Use the existing data row handler
  opts.ctx = &data;
  data.end_offset_limit = cdata->end_offset - cdata->start_offset; // Set chunk boundary
  data.parser = zsv_new(&opts);

  // 4. Process
  enum zsv_status status = zsv_status_ok;
  while (status == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
    status = zsv_parse_more(data.parser);

  fprintf(stderr, "id %i row count: %zu\n", cdata->id, data.data_row_count);
  // Clean up
  zsv_delete(data.parser);
  fflush(stream);
  fclose(stream);
  zsv_writer_delete(data.csv_writer);
  fclose(writer_opts.stream);

  cdata->status = zsv_status_ok;
  return NULL;
}

// zsv_select_output_data_row(): output row data (No change needed)
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
  if (UNLIKELY(zsv_cell_count(data->parser) == 0 || data->cancelled))
    return;

  data->data_row_count++;

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

static void zsv_select_data_row_parallel(void *ctx) {
  struct zsv_select_data *data = ctx;

  if (UNLIKELY(zsv_cum_scanned_length(data->parser) > data->end_offset_limit)) {
    data->cancelled = 1;
    return;
  }
  zsv_select_data_row(ctx);
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
  if (zsv_select_set_output_columns(data)) {
    data->cancelled = 1;
    return;
  }

  if (data->run_in_parallel) {
    struct zsv_parallel_data *pdata = data->parallel_data;
    zsv_select_print_header_row(data);

    // start worker threads
    for (int i = 1; i < data->num_chunks; i++) {
      struct zsv_chunk_data *cdata = &pdata->chunk_data[i];
      cdata->id = i;
      // cdata->input_path = pdata->chunk_data[0].input_path; // Use the path from chunk 1
      cdata->opts = data->opts;

      int create_status = pthread_create(&pdata->threads[i - 1], NULL, zsv_process_chunk, cdata);
      if (create_status != 0) {
        data->cancelled = 1;
        zsv_printerr(1, "Error creating worker thread for chunk %d: %s\n", i, strerror(create_status));
        return;
      }
    }

    // 3. Main thread continues processing Chunk 1
    zsv_set_row_handler(data->parser, zsv_select_data_row_parallel);
  } else {
    // Original serial logic
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

  if (data->run_in_parallel)
    zsv_parallel_data_delete(data->parallel_data);
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
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  int col_index_arg_i = 0;
  unsigned char *preview_buff = NULL;
  size_t preview_buff_len = 0;

  enum zsv_status stat = zsv_status_ok;
  for (int arg_i = 1; stat == zsv_status_ok && arg_i < argc; arg_i++) {
    // ... (Argument parsing remains the same) ...
    // ... (See original code for this section) ...
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
#ifndef NO_PARALLEL
    } else if (!strcmp(argv[arg_i], "-j") || !strcmp(argv[arg_i], "--jobs")) {
      if (!(arg_i + 1 < argc && atoi(argv[arg_i + 1]) >= 2))
        stat = zsv_printerr(1, "%s option value invalid: should be an integer => 2; got %s", argv[arg_i],
                            arg_i + 1 < argc ? argv[arg_i + 1] : "");
      else
        data.num_chunks = (unsigned)atoi(argv[++arg_i]);
    } else if (!strcmp(argv[arg_i], "--parallel")) {
      data.num_chunks = zsv_get_number_of_cores();
#endif
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
      data.input_path = argv[arg_i];
  }

  // Set default output stream if none specified
  if (!writer_opts.stream) {
    writer_opts.stream = stdout;
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

  if (data.opts->verbose) {
    if (data.num_chunks > 1)
      fprintf(stderr, "Running parallelized with %u jobs\n", data.num_chunks);
    else
      fprintf(stderr, "Running single-threaded\n");
  }
  // Only attempt parallelization for a file and if it meets the size threshold
  if (stat == zsv_status_ok && data.input_path && data.opts->stream != stdin && data.num_chunks > 1) {
    // NOTE: File size check should ideally be done with fstat() on the file descriptor.
    // Assuming a valid file is open and we can infer its size or use a heuristic.
    // For this example, we assume we can get the file size.
    // file_size = get_file_size(input_path);
    // if (file_size >= PARALLEL_THRESHOLD_BYTES) {

    // NOTE: We MUST ensure the FILE* is at the start for zsv_calculate_file_chunks to work.
    // Since data.opts->stream was just opened (or is NULL if using preview_buff), this is likely fine.
    struct zsv_chunk_position *chunk_offsets = zsv_calculate_file_chunks(data.input_path, data.num_chunks);
    if (chunk_offsets) {
      if (!(data.parallel_data = zsv_parallel_data_new(data.num_chunks))) {
        stat = zsv_status_memory;
        fprintf(stderr, "Out of memory!\n");
      } else {
        struct zsv_parallel_data *pdata = data.parallel_data;
        data.run_in_parallel = 1;
        pdata->main_data = &data;

        // Configure Chunk 1 for the main thread
        data.end_offset_limit = chunk_offsets[0].end;

        // Prepare chunk data for workers
        for (int i = 0; i < data.num_chunks; i++) {
          pdata->chunk_data[i].start_offset = chunk_offsets[i].start;
          pdata->chunk_data[i].end_offset = chunk_offsets[i].end;
          fprintf(stderr, "Chunk %i: %zu - %zu\n", i, (size_t)pdata->chunk_data[i].start_offset,
                  (size_t)pdata->chunk_data[i].end_offset);
        }

        // The main thread's parser will start at chunk_offsets[0][0]
        //              if (fseeko(data.opts->stream, pdata->chunk_data[0].start_offset, SEEK_SET) != 0) {
        //                  stat = zsv_printerr(1, "Error seeking to start offset of chunk 1\n");
        //                  data.run_in_parallel = 0;
        //              }
      }
      zsv_free_chunks(chunk_offsets);
    } else {
      // Chunk calculation failed, fall back to serial
      data.run_in_parallel = 0;
    }
    // }
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
      if (zsv_new_with_properties(data.opts, custom_prop_handler, data.input_path, &data.parser) == zsv_status_ok) {
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

        // Main thread processes Chunk 1 (or the whole file if not parallel)
        while (status == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
          status = zsv_parse_more(data.parser);

        if (status == zsv_status_no_more_input)
          status = zsv_finish(data.parser);

        zsv_delete(data.parser);

        if (data.run_in_parallel) {
          struct zsv_parallel_data *pdata = data.parallel_data;
          // Wait for all worker threads to finish (Join)
          for (int i = 0; i < data.num_chunks - 1; i++) {
            pthread_join(pdata->threads[i], NULL);
          }

          // Serialize Output (Chunk 2, 3, 4) - Minimizing performance impact
          // Write the output buffers in order (2, 3, 4) to the main output stream
          // TO DO: use concatenate_copy() here!

          // we must make sure the main thread's writer has finished
          // and explicitly flush the file since we will use file descriptors
          // it is also necessary to use zsv_writer_delete() to flush the final
          zsv_writer_delete(data.csv_writer);
          data.csv_writer = NULL; // prevent double-free
          fflush(writer_opts.stream);
          int out_fd = fileno(writer_opts.stream); // Get FD of the main output stream
          for (int i = 1; i < data.num_chunks; i++) {
            struct zsv_chunk_data *c = &pdata->chunk_data[i];
            const char *tmp_filename = c->tmp_output_filename;

            // Open the temporary chunk file
            int in_fd = open(tmp_filename, O_RDONLY);
            if (in_fd < 0) {
              zsv_printerr(1, "Error opening chunk file %s: %s\n", tmp_filename, strerror(errno));
              stat = zsv_status_error;
              break;
            }

            // Get file size (necessary for safe, fixed-length copy)
            struct stat st;
            if (fstat(in_fd, &st) != 0) {
              zsv_printerr(1, "Error stat'ing chunk file %s: %s\n", tmp_filename, strerror(errno));
              close(in_fd);
              stat = zsv_status_error;
              break;
            }
            off_t file_size = st.st_size;

            // Use cross-platform/zero-copy utility to transfer data
            long bytes_copied = concatenate_copy(out_fd, in_fd, file_size);

            close(in_fd); // Close the input chunk file FD

            if (bytes_copied != file_size) {
              zsv_printerr(1, "Warning: Failed to copy all output from chunk %d: copied %lli\n", i, bytes_copied);
            }

            // Delete the temporary file immediately after use
            if (unlink(tmp_filename) != 0) {
              zsv_printerr(1, "Warning: Failed to delete temporary file %s: %s\n", tmp_filename,
                           strerror(errno));
            }
          }
        }
        // ====================================================================
      }
    }
  }
  fprintf(stderr, "id %i row count: %zu\n", 0, data.data_row_count);

  free(preview_buff);
  zsv_select_cleanup(&data);
  if (writer_opts.stream && writer_opts.stream != stdout)
    fclose(writer_opts.stream);
  return stat;
}
