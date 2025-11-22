/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

// Parallelization TO DO
// 1. after processing chunk N, once start is verified, verify starting position of chunk N+1. if err:
//   - exit, or
//   - reprocess N+1
// 2. disallow parallelization when certain options are enabled:
//    - max_rows
//    - overwrite_auto
//    - overwrite
// 3. Minimal file size, header offset

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Parallel support headers
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZSV_COMMAND count
#include "zsv_command.h"
#include "zsv/utils/arg.h"
#include "zsv/utils/os.h"
#include "utils/zsv_chunk.h"

struct data {
  zsv_parser parser;
  size_t rows;
  size_t end_offset_limit; // Parallel: stop parsing after this offset
};

struct chunk_opts {
  struct zsv_opts opts; // Copy of global options
  struct data data;     // Local data (counter/parser)
  const char *input_path;
  size_t start;
  size_t end;
  int id;
};

static void row_verbose(void *ctx) {
  struct data *d = (struct data *)ctx;
  d->rows++;
  if ((d->rows - 1) % 1000000 == 0)
    fprintf(stderr, "Processed %zumm data rows\n", (d->rows - 1) / 1000000);
}

static void row(void *ctx) {
  ((struct data *)ctx)->rows++;
}

#ifndef ZSV_NO_PARALLEL
static void row_parallel(void *ctx) {
  struct data *d = (struct data *)ctx;
  if (d->end_offset_limit && zsv_cum_scanned_length(d->parser) > d->end_offset_limit) {
    zsv_abort(d->parser);
    return;
  }
  d->rows++;
}

static void row_verbose_parallel(void *ctx) {
  row_parallel(ctx);
}

static void *count_chunk(void *arg) {
  struct chunk_opts *c_opts = (struct chunk_opts *)arg;
  FILE *stream = fopen(c_opts->input_path, "rb");
  if (!stream) {
    perror("fopen");
    return NULL;
  }
  if (fseeko(stream, c_opts->start, SEEK_SET) != 0) {
    fclose(stream);
    return NULL;
  }

  // Configure parser
  c_opts->opts.stream = stream;
  c_opts->opts.ctx = &c_opts->data;
  c_opts->data.end_offset_limit = c_opts->end - c_opts->start;
  c_opts->opts.row_handler = c_opts->opts.verbose ? row_verbose_parallel : row_parallel;

  if (!(c_opts->data.parser = zsv_new(&c_opts->opts))) {
    perror(NULL);
  } else {
    enum zsv_status status;
    while ((status = zsv_parse_more(c_opts->data.parser)) == zsv_status_ok)
      ;
    zsv_finish(c_opts->data.parser);
    zsv_delete(c_opts->data.parser);
  }

  fclose(stream);
  return NULL;
}
#endif

static int count_usage(void) {
  static const char *usage = "Usage: count [options]\n"
                             "\n"
                             "Options:\n"
                             "  -h,--help             : show usage\n"
                             "  -i,--input <filename> : use specified file input\n"
                             "  -j,--jobs <n>         : use n concurrent jobs (default: 1)\n"
                             "  --parallel            : use number of cores as job count\n";
  printf("%s\n", usage);
  return 0;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  struct data data = {0};
  struct zsv_opts opts = *optsp;
  const char *input_path = NULL;
  int n_jobs = 0;
  int err = 0;

  for (int i = 1; !err && i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      count_usage();
      goto count_done;
    }
    if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
      if (++i >= argc) {
        fprintf(stderr, "%s option requires a number > 0\n", arg);
        err = 1;
      } else {
        n_jobs = atoi(argv[i]);
        if (n_jobs < 1) {
          fprintf(stderr, "%s option requires a number > 0\n", arg);
          err = 1;
        }
      }
    } else if (!strcmp(arg, "--parallel")) {
      n_jobs = zsv_get_number_of_cores();
    } else if (!strcmp(arg, "-i") || !strcmp(arg, "--input") || *arg != '-') {
      if ((!strcmp(arg, "-i") || !strcmp(arg, "--input")) && ++i >= argc) {
        fprintf(stderr, "%s option requires a filename\n", arg);
        err = 1;
      } else {
        if (opts.stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        else {
          input_path = argv[i];
        }
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

  if (input_path && n_jobs > 1 && access(input_path, F_OK) == -1) {
    fprintf(stderr, "Unable to open for reading: %s\n", input_path);
    err = 1;
  }

  // If not parallel, open immediately. If parallel, threads open their own.
  if (n_jobs <= 1 && input_path) {
    if (!(opts.stream = fopen(input_path, "rb"))) {
      fprintf(stderr, "Unable to open for reading: %s\n", input_path);
      err = 1;
    }
  }

#ifdef NO_STDIN
  if (!input_path) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  if (!err) {
    size_t total_rows = 0;

#ifndef ZSV_NO_PARALLEL
    if (n_jobs > 1 && input_path) {
      // Parallel Execution
      struct zsv_chunk_position *chunks = zsv_calculate_file_chunks(input_path, n_jobs, 0, 0);
      if (!chunks) {
        fprintf(stderr, "Unable to calculate file chunks for parallel processing\n");
        goto count_done; // or fallback to serial
      }

      pthread_t *threads = calloc(n_jobs - 1, sizeof(pthread_t));
      struct chunk_opts *t_opts = calloc(n_jobs, sizeof(struct chunk_opts));

      // Launch worker threads (1 to N-1)
      for (int i = 1; i < n_jobs; i++) {
        t_opts[i].opts = opts; // Copy base opts
        t_opts[i].input_path = input_path;
        t_opts[i].start = chunks[i].start;
        t_opts[i].end = chunks[i].end;
        t_opts[i].id = i;

        if (pthread_create(&threads[i - 1], NULL, count_chunk, &t_opts[i]) != 0) {
          fprintf(stderr, "Failed to create thread %d\n", i);
          // Handle error cleanup in production code
        }
      }

      // Process Chunk 0 in Main Thread
      t_opts[0].opts = opts;
      t_opts[0].input_path = input_path;
      t_opts[0].start = chunks[0].start;
      t_opts[0].end = chunks[0].end;
      count_chunk(&t_opts[0]);

      // Join threads
      for (int i = 1; i < n_jobs; i++) {
        pthread_join(threads[i - 1], NULL);
      }

      // Accumulate Results
      for (int i = 0; i < n_jobs; i++) {
        total_rows += t_opts[i].data.rows;
      }

      free(threads);
      free(t_opts);
      zsv_free_chunks(chunks);

    } else
#endif
    {
      // Serial Execution
      if (!opts.stream)
        opts.stream = stdin; // Handle non-file case

      opts.row_handler = opts.verbose ? row_verbose : row;
      opts.ctx = &data;
      if (zsv_new_with_properties(&opts, custom_prop_handler, input_path, &data.parser) != zsv_status_ok) {
        fprintf(stderr, "Unable to initialize parser\n");
        err = 1;
      } else {
        enum zsv_status status;
        while ((status = zsv_parse_more(data.parser)) == zsv_status_ok)
          ;
        zsv_finish(data.parser);
        zsv_delete(data.parser);
        total_rows = data.rows;
      }
    }

    // Output result (subtracting header row if rows exist)
    printf("%zu\n", total_rows > 0 ? total_rows - 1 : 0);
  }

count_done:
  if (opts.stream && opts.stream != stdin)
    fclose(opts.stream);

  return err;
}
