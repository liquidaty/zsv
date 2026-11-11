#ifndef ZSVSHEET_STRUCT_OPS_H
#define ZSVSHEET_STRUCT_OPS_H

#include <stddef.h>

// A row or column inserted at, or deleted from, the cursor's position
enum zsvsheet_struct_op_kind {
  zsvsheet_struct_op_insert,
  zsvsheet_struct_op_delete
};
enum zsvsheet_struct_axis {
  zsvsheet_struct_axis_row,
  zsvsheet_struct_axis_col
};
struct zsvsheet_struct_op {
  enum zsvsheet_struct_op_kind kind;
  enum zsvsheet_struct_axis axis;
  size_t at;     // the cursor's raw row (0: the header) or data column
  char after;    // insert only: after `at` (o, c) rather than before it (O, C)
  size_t widest; // set by the export: cells in the widest row it wrote
};

// The row or column op inserts (at, or after it) or deletes (at)
static inline size_t zsvsheet_struct_op_pos(const struct zsvsheet_struct_op *op) {
  return op->at + (size_t)(op->kind == zsvsheet_struct_op_insert && op->after);
}

// Keys of a <ctrl>o row or column sequence typed so far (see zsvsheet_struct_key())
enum zsvsheet_struct_prefix {
  zsvsheet_struct_prefix_none = 0,
  zsvsheet_struct_prefix_ctrl_o,   // <ctrl>o
  zsvsheet_struct_prefix_ctrl_o_d, // <ctrl>o d
};

#endif
