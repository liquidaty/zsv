#ifndef SHEET_INDEX_H
#define SHEET_INDEX_H

#include <pthread.h>

#include "zsv.h"

struct zsvsheet_indexer {
  zsv_parser parser;
  struct zsv_index *ix;
};

struct zsvsheet_index_opts {
  pthread_mutex_t *mutexp;
  const char *filename;
  struct zsv_opts zsv_opts;
  struct zsvsheet_ui_buffer *uib;
  int *errp;
  struct zsv_prop_handler *custom_prop_handler;
  char *old_ui_status;
};

enum zsv_index_status build_memory_index(struct zsvsheet_index_opts *optsp);

#endif
