#ifndef SHEET_INDEX_H
#define SHEET_INDEX_H

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "zsv.h"
#include "zsv/utils/writer.h"

struct zsvsheet_indexer {
  zsv_parser parser;
  struct zsv_index *ix;
  const char *filter;
  size_t filter_len;
  zsv_csv_writer writer;
  FILE *filter_stream;
  size_t row_num; // 1-based row number (1 = header row, 2 = first data row)
  unsigned char seen_header : 1;
  unsigned char has_row_num : 1;
  unsigned char _ : 6;
};

struct zsvsheet_index_opts {
  pthread_mutex_t *mutexp;
  const char *filename;
  char **data_filenamep;
  const char *row_filter;
  struct zsv_opts zsv_opts;
  struct zsv_index **index;
  unsigned char *index_ready;
  struct zsvsheet_ui_buffer *uib;
  int *errp;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

enum zsv_index_status build_memory_index(struct zsvsheet_index_opts *optsp);

#endif
