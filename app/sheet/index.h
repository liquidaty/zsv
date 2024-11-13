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
  size_t row_num;
  struct zsvsheet_ui_buffer *parent_uib;
  // enum zsvsheet_rownum_display parent_rownum_display;
  char seen_header;
};

struct zsvsheet_index_opts {
  pthread_mutex_t *mutexp;
  const char *filename;
  const char *row_filter;
  struct zsv_opts zsv_opts;
  struct zsvsheet_ui_buffer *uib;
  struct zsvsheet_ui_buffer *parent_uib;
  int *errp;
  // enum zsvsheet_rownum_display parent_rownum_display;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

enum zsv_index_status build_memory_index(struct zsvsheet_index_opts *optsp);

#endif
