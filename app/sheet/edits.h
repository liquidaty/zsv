#ifndef ZSVSHEET_EDITS_H
#define ZSVSHEET_EDITS_H

#include <stddef.h>

// One edited cell. row: raw input row (0 = header); col: 0-based data column
struct zsvsheet_cell_edit {
  size_t row;
  size_t col;
  unsigned char *value; // NUL-terminated copy owned by the container
  size_t len;
};

// Edited cells sorted by (row, col), at most one entry per cell
struct zsvsheet_edits {
  struct zsvsheet_cell_edit *items;
  size_t count;
  size_t allocated;
};

// Insert or replace the value at (row, col). Returns 0, or ENOMEM with `e` unchanged
int zsvsheet_edits_set(struct zsvsheet_edits *e, size_t row, size_t col, const unsigned char *value, size_t len);

// Index of the first entry at or after (row, col); e->count if there is none
size_t zsvsheet_edits_lower_bound(const struct zsvsheet_edits *e, size_t row, size_t col);

// Free every entry; `e` is empty and reusable afterwards
void zsvsheet_edits_clear(struct zsvsheet_edits *e);

#endif
