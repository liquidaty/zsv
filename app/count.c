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
#include <errno.h>

#define ZSV_COMMAND count
#include "zsv_command.h"

struct bounds {
  size_t start;
  size_t end;
};

struct data {
  zsv_parser parser;
  struct zsv_opts *opts;
  size_t rows;
  const char *input_path;
  unsigned chunk_i;
  struct bounds bounds;
};

static void row(void *ctx) {
  ((struct data *)ctx)->rows++;
}

static int count_usage(void) {
  static const char *usage =
    "Usage: count [options]\n"
    "Options:\n"
    " -h, --help            : show usage\n"
    " [-i, --input] <filename>: use specified file input\n";
  printf("%s\n", usage);
  return 0;
}

#define TEST_N 8

///
#include <pthread.h>

static size_t unquoted_chunk_end(size_t start, FILE *f) {
  char minibuff[1024];
  size_t bytes_read;
  if (
#ifdef _WIN32
      _fseeki64
#else
      fseeko
#endif
      (f, start, SEEK_SET) == -1) {
    perror("fseeko"); fflush(stderr);
    return 0;
  }

  while ((bytes_read = fread(minibuff, 1, sizeof(minibuff), f)) > 0) {
    char *found = memchr(minibuff, '\n', bytes_read);
    if (found)
      return start + (found - minibuff) + 1;
    start += bytes_read;
    if (bytes_read < sizeof(minibuff))
      break;
  }
  return start;
}

size_t get_filesize(const char *filename) {
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(filename, &st) != 0) {
        perror("stat");
        return EXIT_FAILURE;
    }
#else  
  struct stat st; // to do: 64-bit for win
  if (stat(filename, &st) == -1) {
    perror("stat");
    return 0;
  }
#endif
  return st.st_size;
}

struct bounds next_unquoted_chunk_boundaries(FILE *f, size_t filesize, size_t chunk_size, unsigned N, unsigned max_N, size_t start) {
  struct bounds bounds = { 0 };
  size_t end = start + chunk_size > filesize ? filesize : unquoted_chunk_end(start + chunk_size, f);
  size_t size = (N == max_N - 1) ? (filesize - start) : end - start;
  bounds.start = start; // to do: if N == 0, this comes at the end of the header
  bounds.end = start + size;
  return bounds;
}

struct csvindex_file {
  FILE *f;
  size_t start;
  size_t length;
  size_t bytes_remaining;
  int err_no;
};

void csvindex_file_delete(struct csvindex_file *csvif) {
  if(csvif->f)
    fclose(csvif->f);
  free(csvif);
}

struct csvindex_file *csvindex_file_new(const char *filename, size_t start, size_t length) {
  struct csvindex_file *csvif = calloc(1, sizeof(*csvif));
  if(csvif) {
    csvif->f = fopen(filename, "rb");
    csvif->start = start;
    csvif->length = length;
    csvif->bytes_remaining = length;
  }
  if(csvif && !csvif->f) {
    csvindex_file_delete(csvif);
    return NULL;
  }
  return csvif;
}

int csvindex_file_prepare(struct csvindex_file *csvif) {
  // somewhere around here we should get the header
#ifdef _WIN32
  _fseeki64(csvif->f, csvif->start, SEEK_SET);
#else
  fseeko(csvif->f, csvif->start, SEEK_SET);
#endif
  return 0;
}

size_t csvindex_chunk_read(void * restrict buff, size_t n, size_t size, void * restrict ctx) {
  size_t bytes_to_read = n * size;
  struct csvindex_file *csvif = ctx;
  if(bytes_to_read > csvif->bytes_remaining)
    bytes_to_read = csvif->bytes_remaining;
  if(!bytes_to_read)
    return 0;

  size_t bytes_read = fread(buff, 1, bytes_to_read, csvif->f);
  if(!bytes_read) {
    csvif->bytes_remaining = 0;
    csvif->err_no = errno;
  } else
    csvif->bytes_remaining -= bytes_read;
  return bytes_read;
}

static void *process_chunk(void *d) {
  struct data *data = d;
  int chunk_i = data->chunk_i;
  size_t start = data->bounds.start;
  size_t end = data->bounds.end;
  struct csvindex_file *csvif = csvindex_file_new(data->input_path, start, end - start);
  if(!csvif) {
    perror(data->input_path);
  } else {
    csvindex_file_prepare(csvif);
    zsv_parser parser = NULL;
    struct zsv_opts opts = *data->opts;
    opts.row_handler = row;
    opts.ctx = d;
    opts.read = csvindex_chunk_read;
    opts.stream = csvif;

    enum zsv_status stat = zsv_status_ok;
    if(chunk_i == 0)
      // stat = zsv_new_with_properties(&opts, custom_prop_handler, data->input_path, opts_used, &parser);
      stat = zsv_new_with_properties(&opts, NULL, data->input_path, NULL, &parser);
    else
      parser = zsv_new(&opts);
    if(!parser || stat != zsv_status_ok) {
      fprintf(stderr, "Unable to initialize parser\n"), fflush(stderr);
    } else {
      enum zsv_status status;
      while((status = zsv_parse_more(parser)) == zsv_status_ok)
        ;
      zsv_finish(parser);
      zsv_delete(parser);
    }
    csvindex_file_delete(csvif);
  }
  fprintf(stderr, "DONE PROCESSING %d: %d\n", chunk_i, __LINE__), fflush(stderr);
  return NULL;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  struct data data = { 0 };
  int err = 0;
  for(int i = 1; !err && i < argc; i++) {
    const char *arg = argv[i];
    if(!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      count_usage();
      goto count_done;
    } if(!strcmp(arg, "-i") || !strcmp(arg, "--input") || *arg != '-') {
      err = 1;
      if((!strcmp(arg, "-i") || !strcmp(arg, "--input")) && ++i >= argc)
        fprintf(stderr, "%s option requires a filename\n", arg);
      else {
        if(opts->stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        //        else if(!(opts->stream = fopen(argv[i], "rb")))
        //          fprintf(stderr, "Unable to open for reading: %s\n", argv[i]);
        else {
          data.input_path = argv[i];
          err = 0;
        }
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

  if(!data.input_path)
    fprintf(stderr, "Please specify an input file\n"), err = 1;
    
#ifdef NO_STDIN
  if(!opts->stream || opts->stream == stdin) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  size_t filesize = get_filesize(data.input_path);
  if(!filesize) err = 1;
  FILE *f = fopen(data.input_path, "rb");
  if(!f) {
    perror(data.input_path);
    err = 1;
  }
  if(!err) {
    unsigned max_N = TEST_N;
    struct data thread_data[TEST_N] = { 0 };
    pthread_t threads[TEST_N] = { 0 };
    size_t chunk_size = filesize / max_N;
    struct bounds bounds = { 0 };
    unsigned N;
    for(N = 0; N < max_N && bounds.start < filesize; N++) {
      bounds = next_unquoted_chunk_boundaries(f, filesize, chunk_size, N, max_N, bounds.end);
      data.chunk_i = N;
      thread_data[N] = data;
      thread_data[N].opts = opts;
      thread_data[N].bounds = bounds;
      pthread_create(&threads[N], NULL, process_chunk, (void *)&thread_data[N]);
    }
    for(unsigned i = 0; i < N; i++) {
      void *value_ptr = NULL;
      fprintf(stderr, "JOINING %i\n", i), fflush(stderr);
      if(pthread_join(threads[i],&value_ptr))
        perror("pthread_join error");
      else
        data.rows += thread_data[i].rows;
    }
    printf("%zu\n", data.rows > 0 ? data.rows - 1 : 0);
  }
  if(f)
    fclose(f);

 count_done:
  if(opts->stream && opts->stream != stdin)
    fclose(opts->stream);

  return err;
}
