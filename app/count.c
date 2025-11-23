/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h> // off_t

#define ZSV_COMMAND count
#include "zsv_command.h"
#include <zsv/utils/file.h>
#include <zsv/utils/os.h> // zsv_get_number_of_cores
#include "utils/chunk.h"

#define ZSV_COUNT_PARALLEL_MIN_BYTES (1024 * 1024 * 2)

/* * ---------------------------------------------------------------------------
 * Parallel Structures (Inlined to remove select/parallel.c dependency)
 * ---------------------------------------------------------------------------
 */

struct zsv_chunk_data {
  unsigned int id;
  size_t start_offset;
  size_t end_offset;

  /* Outputs */
  size_t actual_next_row_start;
  size_t row_count;
  int status;

  /* Inputs */
  const char *input_path;
  struct zsv_opts *opts_template;

  /* Runtime state for workers */
  int skip;
};

struct zsv_parallel_data {
  unsigned int chunk_count;
  struct zsv_chunk_data *chunks;
  pthread_t *threads;
};

struct data {
  zsv_parser parser;
  size_t rows;

  /* Configuration */
  struct zsv_opts *opts;
  const char *input_path;
  unsigned int num_chunks;

  /* Parallel State */
  int run_in_parallel;
  struct zsv_parallel_data *pdata;

  /* Chunk 0 / Boundary State */
  size_t end_offset_limit; /* Where this chunk (chunk 0) should stop */
  int cancelled;           /* Stops the parse loop if chunk limit reached */
  size_t next_row_start;   /* Where chunk 0 actually ended */
};

/* Forward declaration */
static void *process_chunk_internal(struct zsv_chunk_data *cdata);

/* * ---------------------------------------------------------------------------
 * Helper: Parallel Data Management
 * ---------------------------------------------------------------------------
 */
static struct zsv_parallel_data *parallel_data_new(unsigned int count) {
  struct zsv_parallel_data *pd = calloc(1, sizeof(*pd));
  if (!pd)
    return NULL;
  pd->chunk_count = count;
  pd->chunks = calloc(count, sizeof(*pd->chunks));
  pd->threads = calloc(count, sizeof(*pd->threads));
  if (!pd->chunks || !pd->threads) {
    free(pd->chunks);
    free(pd->threads);
    free(pd);
    return NULL;
  }
  return pd;
}

static void parallel_data_delete(struct zsv_parallel_data *pd) {
  if (pd) {
    free(pd->chunks);
    free(pd->threads);
    free(pd);
  }
}

/* * ---------------------------------------------------------------------------
 * Row Handlers
 * ---------------------------------------------------------------------------
 */

/* Serial / Standard Handlers */
static void row_verbose(void *ctx) {
  struct data *data = ctx;
  data->rows++;
  if (data->rows % 1000000 == 0)
    fprintf(stderr, "Processed %zu data rows\n", data->rows / 1000000);
}

static void row_simple(void *ctx) {
  ((struct data *)ctx)->rows++;
}

/* Chunk 0 (Main Thread) End Handler */
static void row_parallel_done(void *ctx) {
  struct data *data = ctx;
  // We are finding the start of the row *after* the boundary
  data->next_row_start = zsv_cum_scanned_length(data->parser) - zsv_row_length_raw_bytes(data->parser);
  zsv_abort(data->parser);
  data->cancelled = 1;
}

/* Chunk 0 (Main Thread) Parallel Handler */
static void row_parallel(void *ctx) {
  struct data *data = ctx;
  data->rows++;

  if (UNLIKELY((off_t)zsv_cum_scanned_length(data->parser) >= data->end_offset_limit)) {
    // We crossed the boundary. We must finish this row, then stop.
    // Switch handler to 'done' to catch the exact end of this row.
    zsv_set_row_handler(data->parser, row_parallel_done);
  }
}

/* Worker Thread Row Context */
struct worker_ctx {
  struct zsv_chunk_data *cdata;
  zsv_parser parser;
  size_t limit_len;
  int cancelled;
};

static void worker_row_done(void *ctx) {
  struct worker_ctx *wctx = ctx;
  // Calculate absolute offset of the *next* row start
  size_t scanned = zsv_cum_scanned_length(wctx->parser);
  wctx->cdata->actual_next_row_start = wctx->cdata->start_offset + scanned - zsv_row_length_raw_bytes(wctx->parser);
  zsv_abort(wctx->parser);
  wctx->cancelled = 1;
}

static void worker_row(void *ctx) {
  struct worker_ctx *wctx = ctx;
  wctx->cdata->row_count++;

  if (UNLIKELY((off_t)zsv_cum_scanned_length(wctx->parser) >= wctx->limit_len)) {
    zsv_set_row_handler(wctx->parser, worker_row_done);
  }
}

/* * ---------------------------------------------------------------------------
 * Worker Thread Execution
 * ---------------------------------------------------------------------------
 */
static void *process_chunk_thread(void *arg) {
  struct zsv_chunk_data *cdata = arg;
  return process_chunk_internal(cdata);
}

static void *process_chunk_internal(struct zsv_chunk_data *cdata) {
  cdata->row_count = 0;
  cdata->status = 0;

  if (cdata->start_offset >= cdata->end_offset) {
    cdata->actual_next_row_start = cdata->start_offset;
    cdata->skip = 1;
    return NULL;
  }

  struct zsv_opts opts = *cdata->opts_template;
  struct worker_ctx wctx = {0};
  wctx.cdata = cdata;
  wctx.limit_len = cdata->end_offset - cdata->start_offset;

  FILE *f = fopen(cdata->input_path, "rb");
  if (!f) {
    cdata->status = zsv_status_error;
    return NULL;
  }

  if (fseeko(f, cdata->start_offset, SEEK_SET) != 0) {
    fclose(f);
    cdata->status = zsv_status_error;
    return NULL;
  }

  opts.stream = f;
  opts.ctx = &wctx;
  opts.row_handler = worker_row;

  wctx.parser = zsv_new(&opts);
  if (wctx.parser == NULL) {
    fclose(f);
    cdata->status = zsv_status_error;
    return NULL;
  }

  enum zsv_status status = zsv_status_ok;
  while (status == zsv_status_ok && !wctx.cancelled) {
    status = zsv_parse_more(wctx.parser);
  }

  // If finished naturally (EOF)
  if (!wctx.cancelled) {
    cdata->actual_next_row_start = cdata->start_offset + zsv_cum_scanned_length(wctx.parser);
  }

  zsv_finish(wctx.parser);
  zsv_delete(wctx.parser);
  fclose(f);
  return NULL;
}

/* * ---------------------------------------------------------------------------
 * Header & Setup Logic
 * ---------------------------------------------------------------------------
 */

static void header_handler(void *ctx) {
  struct data *data = ctx;

  /* 1. Calculate Header End */
  size_t header_end = zsv_cum_scanned_length(data->parser);

  /* 2. Decide Strategy */
  int setup_ok = 0;

  if (data->input_path && data->num_chunks > 1) {
    /* Try to guess chunks */
    struct zsv_chunk_position *offsets =
      zsv_guess_file_chunks(data->input_path, data->num_chunks, ZSV_COUNT_PARALLEL_MIN_BYTES, header_end);

    if (offsets) {
      data->pdata = parallel_data_new(data->num_chunks);
      if (!data->pdata) {
        zsv_free_chunks(offsets);
        fprintf(stderr, "Insufficient memory for parallelization\n");
      } else {
        data->run_in_parallel = 1;

        if (data->opts->verbose) {
          for (unsigned int i = 0; i < data->num_chunks; i++) {
            fprintf(stderr, "Chunk %i: %zu - %zu\n", i + 1, offsets[i].start, offsets[i].end);
          }
        }

        /* Setup Worker Chunks (1..N) */
        for (unsigned int i = 1; i < data->num_chunks; i++) {
          struct zsv_chunk_data *c = &data->pdata->chunks[i];
          c->id = i;
          c->start_offset = offsets[i].start;
          c->end_offset = offsets[i].end;
          c->input_path = data->input_path;
          c->opts_template = data->opts;

          if (pthread_create(&data->pdata->threads[i], NULL, process_chunk_thread, c) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i);
            data->run_in_parallel = 0;
            break;
          }
        }

        if (data->run_in_parallel) {
          /* Setup Chunk 0 (Main Thread) */
          data->end_offset_limit = offsets[0].end; // Absolute offset

          /* * CRITICAL: Switch handler on the LIVE parser.
           * Do NOT abort. Do NOT seek. Just change the function pointer.
           */
          zsv_set_row_handler(data->parser, row_parallel);
          setup_ok = 1;
        }
      }
      zsv_free_chunks(offsets);
    }
  }

  if (!setup_ok) {
    /* Fallback to Serial */
    data->run_in_parallel = 0;
    zsv_set_row_handler(data->parser, data->opts->verbose ? row_verbose : row_simple);
  }
}

/* * ---------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------------
 */

static int count_usage(void) {
  static const char *usage = "Usage: count [options]\n"
                             "\n"
                             "Options:\n"
                             "  -h,--help             : show usage\n"
                             "  -i,--input <filename> : use specified file input\n"
                             "  -j,--jobs <n>         : number of jobs (parallel threads)\n"
                             "  --parallel            : use all available cores\n";
  printf("%s\n", usage);
  return 0;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  struct data data = {0};
  struct zsv_opts opts = *optsp;
  data.opts = &opts;

  int err = 0;
  for (int i = 1; !err && i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      count_usage();
      goto count_done;
    }
    if (!strcmp(arg, "-i") || !strcmp(arg, "--input") || *arg != '-') {
      err = 1;
      if ((!strcmp(arg, "-i") || !strcmp(arg, "--input")) && ++i >= argc)
        fprintf(stderr, "%s option requires a filename\n", arg);
      else {
        if (opts.stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        else if (!(opts.stream = fopen(argv[i], "rb")))
          fprintf(stderr, "Unable to open for reading: %s\n", argv[i]);
        else {
          data.input_path = argv[i];
          err = 0;
        }
      }
    } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
      if (++i >= argc)
        err = 1;
      else
        data.num_chunks = atoi(argv[i]);
    } else if (!strcmp(arg, "--parallel")) {
      data.num_chunks = zsv_get_number_of_cores();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

#ifdef NO_STDIN
  if (!opts.stream || opts.stream == stdin) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  if (!err) {
    /* Start with Header Handler */
    opts.row_handler = header_handler;
    opts.ctx = &data;

    if (zsv_new_with_properties(&opts, custom_prop_handler, data.input_path, &data.parser) != zsv_status_ok) {
      fprintf(stderr, "Unable to initialize parser\n");
      err = 1;
    } else {
      enum zsv_status status;

      /* Main Parse Loop */
      while (!data.cancelled && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;

      zsv_finish(data.parser);

      /* Handle Parsing Results */
      if (data.run_in_parallel) {
        /* 1. Finalize Chunk 0 */
        if (!data.next_row_start) {
          // If cancelled wasn't set, we hit EOF
          data.next_row_start = zsv_cum_scanned_length(data.parser);
        }
        // Note: We assume chunk 0 starts at 0, but strictly speaking header_end is handled implicitly
        // because we continued parsing with the SAME parser.

        size_t total_rows = data.rows;

        /* 2. Join and Merge */
        for (unsigned int i = 1; i < data.num_chunks; i++) {
          pthread_join(data.pdata->threads[i], NULL);

          struct zsv_chunk_data *prev_chunk = (i == 1) ? NULL : &data.pdata->chunks[i - 1];
          struct zsv_chunk_data *curr_chunk = &data.pdata->chunks[i];

          /* Determine where the previous chunk actually ended */
          size_t prev_end = (i == 1) ? data.next_row_start : prev_chunk->actual_next_row_start;

          /* Check Overlap */
          if (prev_end > curr_chunk->start_offset) {
            if (data.opts->verbose) {
              fprintf(stderr, "Overlap detected at chunk %u (expected %zu, got %zu). Reprocessing.\n", i,
                      curr_chunk->start_offset, prev_end);
            }
            /* Reprocess Synchronously */
            curr_chunk->start_offset = prev_end;
            process_chunk_internal(curr_chunk);
          }

          total_rows += curr_chunk->row_count;
        }

        printf("%zu\n", total_rows);
        parallel_data_delete(data.pdata);

      } else {
        /* Serial Result */
        printf("%zu\n", data.rows);
      }

      zsv_delete(data.parser);
    }
  }

count_done:
  if (opts.stream && opts.stream != stdin)
    fclose(opts.stream);

  return err;
}
