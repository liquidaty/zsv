#ifndef SHEET_INDEX_H
#define SHEET_INDEX_H

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "zsv.h"
#include "zsv/utils/writer.h"

// Decides the number of rows we skip when storing the line end
// 1 << 10 = 1024 means that we store every 1024th line end
#define LINE_END_SHIFT 10
#define LINE_END_N (1 << LINE_END_SHIFT)

enum zsvsheet_index_status {
  zsvsheet_index_status_ok = 0,
  zsvsheet_index_status_memory,
  zsvsheet_index_status_error,
  zsvsheet_index_status_utf8,
};

struct zsvsheet_index {
  uint64_t header_line_end;
  uint64_t row_count;
  size_t line_end_capacity;
  size_t line_end_len;
  uint64_t line_ends[];
};

struct zsvsheet_indexer {
  zsv_parser parser;
  struct zsvsheet_index *ix;
  const char *filter;
  size_t filter_len;
  zsv_csv_writer writer;
  FILE *filter_stream;
};

struct zsvsheet_index_opts {
  pthread_mutex_t *mutexp;
  const char *filename;
  char **temp_filename;
  const char *row_filter;
  struct zsv_opts *zsv_optsp;
  struct zsvsheet_index **index;
  unsigned char *index_ready;
  struct zsvsheet_ui_buffer *uib;
  int *errp;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

enum zsvsheet_index_status build_memory_index(struct zsvsheet_index_opts *optsp);
void get_memory_index(struct zsvsheet_index *ix, uint64_t row, off_t *offset_out, size_t *remaining_rows_out);

#endif
