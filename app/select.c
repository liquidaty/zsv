/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <assert.h>
#ifdef _WIN32
#define _CRT_RAND_S /* for random number generator, used when sampling. must come before including stdlib.h */
#else
#include <sys/types.h> // off_t
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

// Added for pthreads and parallel I/O management
#include <pthread.h>
#include <string.h> // memcpy, free, etc.

#define ZSV_COMMAND select
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/string.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/memmem.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/os.h>
#include <zsv/utils/file.h>
#include "utils/chunk.h"

#include "select/internal.h" // various defines and structs
#include "select/usage.c"    // zsv_select_usage()
#include "select/rand.c"     // demo_random_bw_1_and_100()
#include "select/fixed.c"    // auto_detect_fixed_column_sizes()
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

#ifndef ZSV_NO_PARALLEL
#include "select/parallel.c" // zsv_parallel_data_new(), zsv_parallel_data_delete()

#define ZSV_SELECT_PARALLEL_MIN_BYTES (1024 * 1024 * 2) // don't parallelize if < 2 MB of data (after header)
#define ZSV_SELECT_PARALLEL_BUFFER_SZ (1024 * 1024 * 8) // to do: make customizable or dynamic

static void zsv_select_data_row(void *ctx);

static void zsv_select_data_row_parallel_done(void *ctx) {
  struct zsv_select_data *data = ctx;
  data->next_row_start = zsv_cum_scanned_length(data->parser) - zsv_row_length_raw_bytes(data->parser);
  zsv_abort(data->parser);
  data->cancelled = 1;
}
static void zsv_select_data_row_parallel(void *ctx) {
  struct zsv_select_data *data = ctx;
  zsv_select_data_row(ctx);

  if (UNLIKELY((off_t)zsv_cum_scanned_length(data->parser) >= data->end_offset_limit)) {
    // parse one more row to get accurate next-row start
    zsv_set_row_handler(data->parser, zsv_select_data_row_parallel_done);
  }
}

static void *zsv_select_process_chunk_internal(struct zsv_chunk_data *cdata) {
  if (cdata->start_offset >= cdata->end_offset) {
    cdata->skip = 1;
    return NULL;
  }

  struct zsv_select_data data = {0}; // local, non-shared zsv_select_data instance

  // Copy necessary setup data from the global context
  memcpy(&data, cdata->opts->ctx, sizeof(data));
  data.parallel_data = NULL; // clear parallel data pointer in local copy
  data.cancelled = 0;        // necessary in case we are re-running due to incorrect chunk start

#ifdef HAVE_PCRE2_8
  // duplicate data.search_regexs for thread safety
  if (data.search_regexs)
    data.search_regexs = zsv_select_regexs_dup(data.search_regexs);
#endif

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

  // set up input
  FILE *stream = fopen(data.input_path, "rb");
  if (!stream) {
    cdata->status = zsv_status_error;
    return NULL;
  }
  fseeko(stream, cdata->start_offset, SEEK_SET);

  // set up output
  struct zsv_csv_writer_options writer_opts = {0};

#ifdef __linux__
  cdata->tmp_output_filename = zsv_get_temp_filename("zsvselect");
  writer_opts.stream = fopen(cdata->tmp_output_filename, "wb");
#else
  if (!(cdata->tmp_f = zsv_memfile_open(ZSV_SELECT_PARALLEL_BUFFER_SZ)) &&
      !(cdata->tmp_f = zsv_memfile_open(ZSV_SELECT_PARALLEL_BUFFER_SZ / 2)) &&
      !(cdata->tmp_f = zsv_memfile_open(ZSV_SELECT_PARALLEL_BUFFER_SZ / 4)) &&
      !(cdata->tmp_f = zsv_memfile_open(ZSV_SELECT_PARALLEL_BUFFER_SZ / 8)))
    cdata->tmp_f = zsv_memfile_open(0);
  writer_opts.stream = cdata->tmp_f;
  writer_opts.write = (size_t(*)(const void *restrict, size_t, size_t, void *restrict))zsv_memfile_write;
#endif

  if (!writer_opts.stream) {
    cdata->status = zsv_status_memory;
    fclose(stream);
    return NULL;
  }
  data.csv_writer = zsv_writer_new(&writer_opts);

  // initialize parser
  opts.stream = stream;
  opts.row_handler = zsv_select_data_row_parallel;
  opts.ctx = &data;
  data.end_offset_limit = cdata->end_offset - cdata->start_offset; // set chunk boundary
  data.parser = zsv_new(&opts);

  // process
  enum zsv_status status = zsv_status_ok;
  while (status == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
    status = zsv_parse_more(data.parser);

#ifndef ZSV_NOPARALLEL
  if (!data.next_row_start)
    // unlikely, but maybe conceivable if chunk split was not accurate and
    // a correctly-split chunk's last row entirely ate the next incorrectly-split chunk
    data.next_row_start = zsv_cum_scanned_length(data.parser) + 1;
#endif

  // clean up
  zsv_delete(data.parser);
#ifdef HAVE_PCRE2_8
  zsv_select_regexs_delete(data.search_regexs);
#endif
  fflush(stream);
  fclose(stream);
  zsv_writer_delete(data.csv_writer);
#ifdef __linux__
  fclose(writer_opts.stream);
#endif
  cdata->actual_next_row_start = data.next_row_start + cdata->start_offset;
  cdata->status = zsv_status_ok;
  return NULL;
}

static void *zsv_select_process_chunk(void *arg) {
  struct zsv_chunk_data *cdata = (struct zsv_chunk_data *)arg;
  return zsv_select_process_chunk_internal(cdata);
}
#endif // ZSV_NO_PARALLEL

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
    if (UNLIKELY(data->any_clean != 0)) {
      // leading/trailing white may have been converted to NULL for regex search
      while (cell.len && *cell.str == '\0')
        cell.str++, cell.len--;
      while (cell.len && cell.str[cell.len - 1] == '\0')
        cell.len--;
      cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
    }
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

#ifndef ZSV_NO_PARALLEL
static int zsv_setup_parallel_chunks(struct zsv_select_data *data, const char *path, size_t header_row_end) {
  if (data->num_chunks <= 1 || !path || !strcmp(path, "-")) {
    data->run_in_parallel = 0;
    return 0;
  }

  struct zsv_chunk_position *offsets =
    zsv_guess_file_chunks(path, data->num_chunks, ZSV_SELECT_PARALLEL_MIN_BYTES, header_row_end + 1
#ifndef ZSV_NO_ONLY_CRLF
                          ,
                          data->opts->only_crlf_rowend
#endif
    );
  if (!offsets)
    return -1; // fall back to serial

  if (!(data->parallel_data = zsv_parallel_data_new(data->num_chunks))) {
    zsv_free_chunks(offsets);
    fprintf(stderr, "Insufficient memory to parallelize!\n");
    return zsv_status_memory;
  }

  data->run_in_parallel = 1;
  data->parallel_data->main_data = data;
  data->end_offset_limit = offsets[0].end;

  for (unsigned int i = 0; i < data->num_chunks; i++) {
    data->parallel_data->chunk_data[i].start_offset = offsets[i].start;
    data->parallel_data->chunk_data[i].end_offset = offsets[i].end;
    if (data->opts->verbose)
      fprintf(stderr, "Chunk %i: %zu - %zu\n", i, (size_t)offsets[i].start, (size_t)offsets[i].end);
  }
  zsv_free_chunks(offsets);
  return 0;
}
#endif // ZSV_NO_PARALLEL

static void zsv_select_header_finish(struct zsv_select_data *data) {
  if (zsv_select_set_output_columns(data)) {
    data->cancelled = 1;
    return;
  }
#ifndef ZSV_NO_PARALLEL
  // set up parallelization; on error, fall back to serial
  // TO DO: option to exit on error (instead of fall back)
  if (data->input_path && data->num_chunks > 1) {
    size_t header_row_end = zsv_cum_scanned_length(data->parser);
    zsv_setup_parallel_chunks(data, data->input_path, header_row_end);
  }
  if (data->opts->verbose)
    fprintf(stderr, "Running %s\n", data->run_in_parallel ? "parallel" : "single-threaded");

  if (data->run_in_parallel) {
    struct zsv_parallel_data *pdata = data->parallel_data;
    zsv_select_print_header_row(data);

    // start worker threads
    for (unsigned int i = 1; i < data->num_chunks; i++) {
      struct zsv_chunk_data *cdata = &pdata->chunk_data[i];
      cdata->id = i;
      cdata->opts = data->opts;

      int create_status = pthread_create(&pdata->threads[i - 1], NULL, zsv_select_process_chunk, cdata);
      if (create_status != 0) {
        data->cancelled = 1;
        zsv_printerr(1, "Error creating worker thread for chunk %d: %s", i, strerror(create_status));
        return;
      }
    }

    // main thread processes chunk 1
    zsv_set_row_handler(data->parser, zsv_select_data_row_parallel);
  } else
#endif
  {
    // no parallelization
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

#ifndef ZSV_NO_PARALLEL
  if (data->run_in_parallel)
    zsv_parallel_data_delete(data->parallel_data);
#endif
}

#define ARG_require_val(tgt, conv_func)                                                                                \
  do {                                                                                                                 \
    if (++arg_i >= argc) {                                                                                             \
      stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i - 1]);                                         \
      goto zsv_select_main_done;                                                                                       \
    }                                                                                                                  \
    tgt = conv_func(argv[arg_i]);                                                                                      \
  } while (0)

#ifndef ZSV_NO_PARALLEL
static int zsv_merge_worker_outputs(struct zsv_select_data *data, FILE *dest_stream) {
  if (!data->run_in_parallel || !data->parallel_data)
    return 0;

  fflush(dest_stream);
#ifdef __linux__
  int out_fd = fileno(dest_stream);
#endif
  int status = 0;

  for (unsigned int i = 0; i < data->num_chunks - 1; i++) {
    pthread_join(data->parallel_data->threads[i], NULL);

    struct zsv_chunk_data *next_chunk = &data->parallel_data->chunk_data[i + 1];
    off_t actual_next_row_start =
      i == 0 ? data->next_row_start : data->parallel_data->chunk_data[i].actual_next_row_start;
    off_t expected_next_row_start = next_chunk->start_offset;
    if (actual_next_row_start > expected_next_row_start) {
      if (data->opts->verbose) {
        fprintf(stderr, "Chunk overlap detected (Prev End: %zu, Next Start: %zu). Reprocessing chunk %d.\n",
                (size_t)actual_next_row_start, (size_t)expected_next_row_start, i + 1);
      }

      // clean up invalid results from the worker thread
      zsv_chunk_data_clear_output(next_chunk);

      // adjust the start offset to the actual next row start
      next_chunk->start_offset = actual_next_row_start;

      // reprocess synchronously on the main thread
      zsv_select_process_chunk_internal(next_chunk);

      if (next_chunk->status != zsv_status_ok) // reprocessing failed!
        status = zsv_status_error;
    }
  }

  // join all of the output files into a single output file
  for (unsigned int i = 1; i < data->num_chunks && status == 0; i++) {
    struct zsv_chunk_data *c = &data->parallel_data->chunk_data[i];
    if (c->skip)
      continue;
#ifdef __linux__
    int in_fd = open(c->tmp_output_filename, O_RDONLY);
    if (in_fd < 0) {
      zsv_printerr(1, "Error opening chunk %s: %s", c->tmp_output_filename, strerror(errno));
      status = zsv_status_error;
      break;
    }

    struct stat st;
    if (fstat(in_fd, &st) == 0) {
      long copied = zsv_concatenate_copy(out_fd, in_fd, st.st_size);
      if (copied != st.st_size) {
        zsv_printerr(1, "Warning: Partial copy chunk %d (%lli/%lli)", i, copied, (long long)st.st_size);
        status = zsv_status_error;
      }
    } else {
      status = zsv_status_error;
    }
    close(in_fd);
#else
    zsv_memfile_rewind(c->tmp_f);
    if (zsv_copy_filelike_ptr(
          c->tmp_f, (size_t(*)(void *restrict ptr, size_t size, size_t nitems, void *restrict stream))zsv_memfile_read,
          dest_stream,
          (size_t(*)(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream))fwrite)) {
      perror("zsv temp mem file");
      status = zsv_status_error;
    }
#endif
  }
  return status;
}
#endif

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsv_select_usage();
    return zsv_status_ok;
  }

  struct zsv_select_data data = {0};
  data.opts = opts;
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  int col_index_arg_i = 0;
  unsigned char *preview_buff = NULL;
  size_t preview_buff_len = 0;
  enum zsv_status stat = zsv_status_ok;

  for (int arg_i = 1; stat == zsv_status_ok && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
    if (!strcmp(arg, "--")) {
      col_index_arg_i = arg_i + 1;
      break;
    }

    if (!strcmp(arg, "-b") || !strcmp(arg, "--with-bom"))
      writer_opts.with_bom = 1;
    else if (!strcmp(arg, "--fixed-auto-max-lines"))
      ARG_require_val(data.fixed.max_lines, atoi);
    else if (!strcmp(arg, "--fixed-auto"))
      data.fixed.autodetect = 1;
    else if (!strcmp(arg, "--fixed")) {
      if (++arg_i >= argc) {
        stat = zsv_printerr(1, "--fixed requires val");
        goto zsv_select_main_done;
      }
      data.fixed.count = 1;
      for (const char *s = argv[arg_i]; *s; s++)
        if (*s == ',')
          data.fixed.count++;
      free(data.fixed.offsets);
      data.fixed.offsets = calloc(data.fixed.count, sizeof(*data.fixed.offsets));
      if (!data.fixed.offsets) {
        stat = zsv_printerr(1, "Out of memory!");
        goto zsv_select_main_done;
      }
      size_t count = 0;
      char *dup = strdup(argv[arg_i]), *tok;
      for (tok = strtok(dup, ","); tok && count < data.fixed.count; tok = strtok(NULL, ",")) {
        if (sscanf(tok, "%zu", &data.fixed.offsets[count++]) != 1)
          stat = zsv_printerr(1, "Invalid offset: %s", tok);
      }
      free(dup);
    } else if (!strcmp(arg, "--distinct"))
      data.distinct = 1;
    else if (!strcmp(arg, "--merge"))
      data.distinct = ZSV_SELECT_DISTINCT_MERGE;
    else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
      if (writer_opts.stream && writer_opts.stream != stdout)
        stat = zsv_printerr(1, "Output specified twice");
      else {
        ARG_require_val(arg, (const char *));
        if (!(writer_opts.stream = fopen(arg, "wb")))
          stat = zsv_printerr(1, "Unable to open %s", arg);
      }
    } else if (!strcmp(arg, "-N") || !strcmp(arg, "--line-number"))
      data.prepend_line_number = 1;
    else if (!strcmp(arg, "-n"))
      data.use_header_indexes = 1;
    else if (!strcmp(arg, "-s") || !strcmp(arg, "--search")) {
      const char *v;
      ARG_require_val(v, (const char *));
      zsv_select_add_search(&data, v);
    }
#ifdef HAVE_PCRE2_8
    else if (!strcmp(arg, "--regex-search")) {
      const char *v;
      ARG_require_val(v, (const char *));
      zsv_select_add_regex(&data, v);
    }
#endif
    else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose"))
      data.verbose = 1;
    else if (!strcmp(arg, "--unescape"))
      data.unescape = 1;
    else if (!strcmp(arg, "-w") || !strcmp(arg, "--whitespace-clean"))
      data.clean_white = 1;
    else if (!strcmp(arg, "--whitespace-clean-no-newline")) {
      data.clean_white = 1;
      data.whitespace_clean_flags = 1;
    } else if (!strcmp(arg, "-W") || !strcmp(arg, "--no-trim"))
      data.no_trim_whitespace = 1;
    else if (!strcmp(arg, "--sample-every"))
      ARG_require_val(data.sample_every_n, atoi);
    else if (!strcmp(arg, "--sample-pct"))
      ARG_require_val(data.sample_pct, atof);
    else if (!strcmp(arg, "--prepend-header")) {
      int err = 0;
      data.prepend_header = zsv_next_arg(++arg_i, argc, argv, &err);
      if (err)
        stat = zsv_status_error;
    } else if (!strcmp(arg, "--no-header"))
      data.no_header = 1;
    else if (!strcmp(arg, "-H") || !strcmp(arg, "--head")) {
      int val;
      ARG_require_val(val, atoi);
      data.data_rows_limit = val + 1;
    } else if (!strcmp(arg, "-D") || !strcmp(arg, "--skip-data"))
      ARG_require_val(data.skip_data_rows, atoi);
#ifndef ZSV_NO_PARALLEL
    else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs"))
      ARG_require_val(data.num_chunks, atoi);
    else if (!strcmp(arg, "--parallel")) {
      data.num_chunks = zsv_get_number_of_cores();
      if (data.num_chunks < 2) {
        fprintf(stderr, "Warning: --parallel specified but only one core found; using -j 4 instead");
        data.num_chunks = 4;
      }
    }
#endif
    else if (!strcmp(arg, "-e")) {
      const char *v;
      ARG_require_val(v, (const char *));
      data.embedded_lineend = *v;
    } else if (!strcmp(arg, "-x")) {
      const char *v;
      ARG_require_val(v, (const char *));
      zsv_select_add_exclusion(&data, v);
    } else if (*arg == '-')
      stat = zsv_printerr(1, "Unrecognized argument: %s", arg);
    else if (data.input_path)
      stat = zsv_printerr(1, "Input specified twice");
    else
      data.input_path = arg;
  }

  if (stat != zsv_status_ok)
    goto zsv_select_main_done;

  // configuration & setup
  if (!writer_opts.stream)
    writer_opts.stream = stdout;
  if (data.sample_pct)
    srand(time(0));
  if (data.use_header_indexes && (stat = zsv_select_check_exclusions_are_indexes(&data)))
    goto zsv_select_main_done;

#ifndef ZSV_NO_PARALLEL
  if (data.num_chunks > 1) {
    enum zsv_chunk_status chstat = zsv_chunkable(data.input_path, data.opts);
    if (chstat != zsv_chunk_status_ok) {
      stat = zsv_printerr(1, "%s", zsv_chunk_status_str(chstat));
      goto zsv_select_main_done;
    }
  }
#endif

  // input stream
  if (data.input_path) {
    if (!(data.opts->stream = fopen(data.input_path, "rb")))
      stat = zsv_printerr(1, "Cannot open %s", data.input_path);
  } else {
#ifdef NO_STDIN
    stat = zsv_printerr(1, "Input file required");
    goto zsv_select_main_done;
#else
    data.opts->stream = stdin;
#endif
  }

  // auto-fixed column detection
  if (data.fixed.autodetect) { // fixed-auto flag
    if (data.fixed.count)
      stat = zsv_printerr(1, "--fixed-auto cannot be used with --fixed");
    else {
      size_t bsz = 1024 * 256;
      if (!(preview_buff = calloc(bsz, 1)))
        stat = zsv_status_memory;
      else
        stat =
          auto_detect_fixed_column_sizes(&data.fixed, data.opts, preview_buff, bsz, &preview_buff_len, opts->verbose);
    }
  }
  if (stat != zsv_status_ok)
    goto zsv_select_main_done;

  // parser initialization
  if (col_index_arg_i) {
    data.col_argv = &argv[col_index_arg_i];
    data.col_argc = argc - col_index_arg_i;
  }

  data.header_names = calloc(data.opts->max_columns, sizeof(*data.header_names));
  data.out2in = calloc(data.opts->max_columns, sizeof(*data.out2in));
  data.csv_writer = zsv_writer_new(&writer_opts);

  if (!data.header_names || !data.out2in || !data.csv_writer) {
    stat = zsv_status_memory;
    goto zsv_select_main_done;
  }

  // execution
  data.opts->row_handler = zsv_select_header_row;
  data.opts->ctx = &data;

  if (zsv_new_with_properties(data.opts, custom_prop_handler, data.input_path, &data.parser) == zsv_status_ok) {
    data.any_clean = !data.no_trim_whitespace || data.clean_white || data.embedded_lineend || data.unescape;

    // apply fixed offsets (whether from --fixed arg or --fixed-auto detection)
    if (data.fixed.count && zsv_set_fixed_offsets(data.parser, data.fixed.count, data.fixed.offsets) != zsv_status_ok)
      data.cancelled = 1;

    unsigned char writer_buff[512];
    zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

    zsv_handle_ctrl_c_signal();

    enum zsv_status p_stat = zsv_status_ok;
    if (preview_buff_len)
      p_stat = zsv_parse_bytes(data.parser, preview_buff, preview_buff_len);

    while (p_stat == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
      p_stat = zsv_parse_more(data.parser);

    if (p_stat == zsv_status_no_more_input) {
      zsv_finish(data.parser);
#ifndef ZSV_NO_PARALLEL
      // unlikely, but maybe conceivable if chunk split was not accurate and
      // a correctly-split chunk's last row entirely ate the next incorrectly-split chunk
      if (data.run_in_parallel && !data.next_row_start)
        data.next_row_start = zsv_cum_scanned_length(data.parser) + 1;
#endif
    }
    zsv_delete(data.parser);

#ifndef ZSV_NO_PARALLEL
    if (data.run_in_parallel) {
      // explicitly flush and delete main writer before merge which uses raw fd
      zsv_writer_delete(data.csv_writer);
      data.csv_writer = NULL;
      if (zsv_merge_worker_outputs(&data, writer_opts.stream) != 0)
        stat = zsv_status_error;
    }
#endif
  }

zsv_select_main_done:
  free(preview_buff);
  zsv_select_cleanup(&data);
  if (writer_opts.stream && writer_opts.stream != stdout)
    fclose(writer_opts.stream);
  return stat;
}
