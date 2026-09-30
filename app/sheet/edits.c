#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "edits.h"

size_t zsvsheet_edits_lower_bound(const struct zsvsheet_edits *e, size_t row, size_t col) {
  size_t lo = 0, hi = e->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const struct zsvsheet_cell_edit *m = &e->items[mid];
    if (m->row < row || (m->row == row && m->col < col))
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

int zsvsheet_edits_set(struct zsvsheet_edits *e, size_t row, size_t col, const unsigned char *value, size_t len) {
  if (len == SIZE_MAX)
    return ENOMEM;
  unsigned char *copy = malloc(len + 1);
  if (!copy)
    return ENOMEM;
  if (len)
    memcpy(copy, value, len);
  copy[len] = '\0';

  size_t i = zsvsheet_edits_lower_bound(e, row, col);
  if (i < e->count && e->items[i].row == row && e->items[i].col == col) {
    free(e->items[i].value);
    e->items[i].value = copy;
    e->items[i].len = len;
    return 0;
  }

  if (e->count == e->allocated) {
    size_t n = e->allocated ? e->allocated * 2 : 16;
    if (n < e->allocated || n > SIZE_MAX / sizeof(*e->items)) {
      free(copy);
      return ENOMEM;
    }
    struct zsvsheet_cell_edit *items = realloc(e->items, n * sizeof(*items));
    if (!items) {
      free(copy);
      return ENOMEM;
    }
    e->items = items;
    e->allocated = n;
  }
  memmove(&e->items[i + 1], &e->items[i], (e->count - i) * sizeof(*e->items));
  e->items[i] = (struct zsvsheet_cell_edit){.row = row, .col = col, .value = copy, .len = len};
  e->count++;
  return 0;
}

void zsvsheet_edits_clear(struct zsvsheet_edits *e) {
  for (size_t i = 0; i < e->count; i++)
    free(e->items[i].value);
  free(e->items);
  memset(e, 0, sizeof(*e));
}
