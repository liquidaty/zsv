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
  //  fprintf(stderr, "chunk %i seek'd to %zu, will process up to %zu\n", cdata->id, cdata->start_offset,
  // cdata->end_offset);

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

  // fprintf(stderr, "id %i row count: %zu\n", cdata->id, data.data_row_count);
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

// --- HELPER MACRO & FUNCTIONS ---

// DRY Principle: Macro to handle argument value checking
#define ARG_require_val(tgt, conv_func) do { \
    if (++arg_i >= argc) { \
        stat = zsv_printerr(1, "%s option requires parameter", argv[arg_i - 1]); \
        goto zsv_select_main_done; \
    } \
    tgt = conv_func(argv[arg_i]); \
} while(0)

static int zsv_merge_worker_outputs(struct zsv_select_data *data, FILE *dest_stream) {
    if (!data->run_in_parallel || !data->parallel_data) return 0;

    fflush(dest_stream);
    int out_fd = fileno(dest_stream);
    int status = 0;

    for (int i = 1; i < data->num_chunks; i++) {
        struct zsv_chunk_data *c = &data->parallel_data->chunk_data[i];
        int in_fd = open(c->tmp_output_filename, O_RDONLY);
        
        if (in_fd < 0) {
            zsv_printerr(1, "Error opening chunk %s: %s\n", c->tmp_output_filename, strerror(errno));
            status = zsv_status_error;
            break;
        }

        struct stat st;
        if (fstat(in_fd, &st) == 0) {
            long copied = concatenate_copy(out_fd, in_fd, st.st_size);
            if (copied != st.st_size)
                zsv_printerr(1, "Warning: Partial copy chunk %d (%lli/%lli)\n", i, copied, (long long)st.st_size);
        } else {
            status = zsv_status_error;
        }

        close(in_fd);
        if (unlink(c->tmp_output_filename) != 0)
            zsv_printerr(1, "Warning: Failed to delete %s\n", c->tmp_output_filename);
    }
    return status;
}

static int zsv_setup_parallel_chunks(struct zsv_select_data *data, const char *path) {
    if (data->num_chunks <= 1 || !path || !strcmp(path, "-")) {
        data->run_in_parallel = 0;
        return 0;
    }

    // Note: Ensure file is valid for seeking before calculation
    struct zsv_chunk_position *offsets = zsv_calculate_file_chunks(path, data->num_chunks);
    if (!offsets) return -1; // Fallback to serial

    if (!(data->parallel_data = zsv_parallel_data_new(data->num_chunks))) {
        zsv_free_chunks(offsets);
        return zsv_status_memory;
    }

    data->run_in_parallel = 1;
    data->parallel_data->main_data = data;
    data->end_offset_limit = offsets[0].end;

    for (int i = 0; i < data->num_chunks; i++) {
        data->parallel_data->chunk_data[i].start_offset = offsets[i].start;
        data->parallel_data->chunk_data[i].end_offset = offsets[i].end;
        if (data->opts->verbose)
            fprintf(stderr, "Chunk %i: %zu - %zu\n", i, (size_t)offsets[i].start, (size_t)offsets[i].end);
    }
    zsv_free_chunks(offsets);
    return 0;
}

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

    // 1. Parse Arguments
    for (int arg_i = 1; stat == zsv_status_ok && arg_i < argc; arg_i++) {
        const char *arg = argv[arg_i];
        if (!strcmp(arg, "--")) { col_index_arg_i = arg_i + 1; break; }
        
        if (!strcmp(arg, "-b") || !strcmp(arg, "--with-bom")) writer_opts.with_bom = 1;
        else if (!strcmp(arg, "--fixed-auto-max-lines")) ARG_require_val(data.fixed.max_lines, atoi);
        else if (!strcmp(arg, "--fixed-auto")) data.fixed.count = -1; // logic flag
        else if (!strcmp(arg, "--fixed")) {
            if (++arg_i >= argc) { stat = zsv_printerr(1, "--fixed requires val"); goto zsv_select_main_done; }
            data.fixed.count = 1;
            for (const char *s = argv[arg_i]; *s; s++) if (*s == ',') data.fixed.count++;
            free(data.fixed.offsets);
            data.fixed.offsets = calloc(data.fixed.count, sizeof(*data.fixed.offsets));
            if (!data.fixed.offsets) { stat = zsv_printerr(1, "Out of memory!\n"); goto zsv_select_main_done; }
            // Simple CSV parsing for offsets (simplified from original to keep LOC low, assuming standard behavior)
            size_t count = 0;
            char *dup = strdup(argv[arg_i]), *tok;
            for(tok = strtok(dup, ","); tok && count < data.fixed.count; tok = strtok(NULL, ","))
                 sscanf(tok, "%zu", &data.fixed.offsets[count++]);
            free(dup);
        }
        else if (!strcmp(arg, "--distinct")) data.distinct = 1;
        else if (!strcmp(arg, "--merge")) data.distinct = ZSV_SELECT_DISTINCT_MERGE;
        else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
            if (writer_opts.stream) stat = zsv_printerr(1, "Output specified twice");
            else {
                ARG_require_val(arg, (const char *));
                if (!(writer_opts.stream = fopen(arg, "wb"))) stat = zsv_printerr(1, "Unable to open %s", arg);
            }
        }
        else if (!strcmp(arg, "-N") || !strcmp(arg, "--line-number")) data.prepend_line_number = 1;
        else if (!strcmp(arg, "-n")) data.use_header_indexes = 1;
        else if (!strcmp(arg, "-s") || !strcmp(arg, "--search")) {
             const char *v; ARG_require_val(v, (const char *)); zsv_select_add_search(&data, v);
        }
#ifdef HAVE_PCRE2_8
        else if (!strcmp(arg, "--regex-search")) {
             const char *v; ARG_require_val(v, (const char *)); zsv_select_add_regex(&data, v);
        }
#endif
        else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) data.verbose = 1;
        else if (!strcmp(arg, "--unescape")) data.unescape = 1;
        else if (!strcmp(arg, "-w") || !strcmp(arg, "--whitespace-clean")) data.clean_white = 1;
        else if (!strcmp(arg, "--whitespace-clean-no-newline")) { data.clean_white = 1; data.whitespace_clean_flags = 1; }
        else if (!strcmp(arg, "-W") || !strcmp(arg, "--no-trim")) data.no_trim_whitespace = 1;
        else if (!strcmp(arg, "--sample-every")) ARG_require_val(data.sample_every_n, atoi);
        else if (!strcmp(arg, "--sample-pct")) ARG_require_val(data.sample_pct, atof);
        else if (!strcmp(arg, "--prepend-header")) {
             int err = 0; data.prepend_header = zsv_next_arg(++arg_i, argc, argv, &err); if(err) stat = zsv_status_error;
        }
        else if (!strcmp(arg, "--no-header")) data.no_header = 1;
        else if (!strcmp(arg, "-H") || !strcmp(arg, "--head")) { 
            int val; ARG_require_val(val, atoi); data.data_rows_limit = val + 1; 
        }
        else if (!strcmp(arg, "-D") || !strcmp(arg, "--skip-data")) ARG_require_val(data.skip_data_rows, atoi);
#ifndef NO_PARALLEL
        else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) ARG_require_val(data.num_chunks, atoi);
        else if (!strcmp(arg, "--parallel")) data.num_chunks = zsv_get_number_of_cores();
#endif
        else if (!strcmp(arg, "-e")) { 
            const char *v; ARG_require_val(v, (const char *)); data.embedded_lineend = *v; 
        }
        else if (!strcmp(arg, "-x")) { const char *v; ARG_require_val(v, (const char *)); zsv_select_add_exclusion(&data, v); }
        else if (*arg == '-') stat = zsv_printerr(1, "Unrecognized argument: %s", arg);
        else if (data.input_path) stat = zsv_printerr(1, "Input specified twice");
        else data.input_path = arg;
    }

    if (stat != zsv_status_ok) goto zsv_select_main_done;

    // 2. Configuration & Setup
    if (!writer_opts.stream) writer_opts.stream = stdout;
    if (data.sample_pct) srand(time(0));
    if (data.use_header_indexes && (stat = zsv_select_check_exclusions_are_indexes(&data))) goto zsv_select_main_done;

    // Input stream setup
    if (data.input_path) {
        if (!(data.opts->stream = fopen(data.input_path, "rb"))) stat = zsv_printerr(1, "Cannot open %s", data.input_path);
    } else {
#ifdef NO_STDIN
        stat = zsv_printerr(1, "Input file required"); goto zsv_select_main_done;
#else
        data.opts->stream = stdin;
#endif
    }

    // Auto-fixed column detection
    if (data.fixed.count == -1) { // fixed-auto flag
        size_t bsz = 1024 * 256;
        if (!(preview_buff = calloc(bsz, 1))) stat = zsv_status_memory;
        else stat = auto_detect_fixed_column_sizes(&data.fixed, data.opts, preview_buff, bsz, &preview_buff_len, opts->verbose);
    }
    if (stat != zsv_status_ok) goto zsv_select_main_done;

    // Parallel Setup
    if (data.input_path && data.num_chunks > 1) {
        stat = zsv_setup_parallel_chunks(&data, data.input_path);
        if (stat == zsv_status_memory) goto zsv_select_main_done;
    }
    
    if (data.opts->verbose) 
        fprintf(stderr, "Running %s\n", data.run_in_parallel ? "parallel" : "single-threaded");

    // 3. Parser Initialization
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

    // 4. Execution
    data.opts->row_handler = zsv_select_header_row;
    data.opts->ctx = &data;

    if (zsv_new_with_properties(data.opts, custom_prop_handler, data.input_path, &data.parser) == zsv_status_ok) {
        data.any_clean = !data.no_trim_whitespace || data.clean_white || data.embedded_lineend || data.unescape;
        
        unsigned char writer_buff[512];
        zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

        zsv_handle_ctrl_c_signal();
        
        enum zsv_status p_stat = zsv_status_ok;
        if (preview_buff_len) p_stat = zsv_parse_bytes(data.parser, preview_buff, preview_buff_len);
        
        while (p_stat == zsv_status_ok && !zsv_signal_interrupted && !data.cancelled)
            p_stat = zsv_parse_more(data.parser);

        if (p_stat == zsv_status_no_more_input) zsv_finish(data.parser);
        zsv_delete(data.parser);

        // Join Parallel Threads & Merge Output
        if (data.run_in_parallel) {
            for (int i = 0; i < data.num_chunks - 1; i++) 
                pthread_join(data.parallel_data->threads[i], NULL);
            
            // Explicitly flush and delete main writer before raw fd merge
            zsv_writer_delete(data.csv_writer); 
            data.csv_writer = NULL;
            
            if (zsv_merge_worker_outputs(&data, writer_opts.stream) != 0)
                stat = zsv_status_error;
        }
    }

    // fprintf(stderr, "id 0 row count: %zu\n", data.data_row_count);

zsv_select_main_done:
    free(preview_buff);
    zsv_select_cleanup(&data);
    if (writer_opts.stream && writer_opts.stream != stdout) fclose(writer_opts.stream);
    return stat;
}
