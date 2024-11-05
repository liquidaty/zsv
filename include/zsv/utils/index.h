#ifndef ZSV_UTILS_INDEX_H
#define ZSV_UTILS_INDEX_H

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "zsv/common.h"

// Decides the number of rows we skip when storing the line end
// 1 << 10 = 1024 means that we store every 1024th line end
#define ZSV_INDEX_ROW_SHIFT 10
#define ZSV_INDEX_ROW_N (1 << ZSV_INDEX_ROW_SHIFT)

enum zsv_index_status {
  zsv_index_status_ok = 0,
  zsv_index_status_memory,
  zsv_index_status_error,
  zsv_index_status_utf8,
};

// An array of uint64_t. Needs to be reallocated to extend the capacity.
// Reallocation can be avoided by adding new arrays instead.
struct zsv_index_array {
  size_t capacity;
  size_t len;
  uint64_t u64s[];
};

struct zsv_index {
  uint64_t header_line_end;
  uint64_t row_count;

  // array containing the offsets of every ZSV_INDEX_ROW_N line end
  struct zsv_index_array *array;
};

struct zsv_index *zsv_index_new(void);
void zsv_index_delete(struct zsv_index *ix);
enum zsv_index_status zsv_index_add_row(struct zsv_index *ix, zsv_parser parser);
enum zsv_index_status zsv_index_row_end_offset(const struct zsv_index *ix, uint64_t row, uint64_t *offset_out,
                                               uint64_t *remaining_rows_out);
enum zsv_index_status zsv_index_seek_row(const struct zsv_index *ix, struct zsv_opts *opts, uint64_t row);

#endif
