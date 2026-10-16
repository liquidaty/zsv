#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hidden-columns.h"

// Index of the first member at or after col; h->count if there is none
static size_t zsvsheet_hidden_cols_lower_bound(const struct zsvsheet_hidden_cols *h, size_t col) {
  size_t lo = 0, hi = h->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (h->cols[mid] < col)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

int zsvsheet_hidden_cols_add(struct zsvsheet_hidden_cols *h, size_t col) {
  size_t i = zsvsheet_hidden_cols_lower_bound(h, col);
  if (i < h->count && h->cols[i] == col)
    return 0;
  if (h->count == h->allocated) {
    size_t n = h->allocated ? h->allocated * 2 : 8;
    size_t *cols;
    if (n < h->allocated || n > SIZE_MAX / sizeof(*h->cols))
      return ENOMEM;
    if (!(cols = realloc(h->cols, n * sizeof(*cols))))
      return ENOMEM;
    h->cols = cols;
    h->allocated = n;
  }
  memmove(&h->cols[i + 1], &h->cols[i], (h->count - i) * sizeof(*h->cols));
  h->cols[i] = col;
  h->count++;
  return 0;
}

int zsvsheet_hidden_cols_remove(struct zsvsheet_hidden_cols *h, size_t col) {
  size_t i = zsvsheet_hidden_cols_lower_bound(h, col);
  if (i >= h->count || h->cols[i] != col)
    return 0;
  memmove(&h->cols[i], &h->cols[i + 1], (h->count - i - 1) * sizeof(*h->cols));
  h->count--;
  return 1;
}

void zsvsheet_hidden_cols_clear(struct zsvsheet_hidden_cols *h) {
  free(h->cols);
  memset(h, 0, sizeof(*h));
}

int zsvsheet_hidden_cols_has(const struct zsvsheet_hidden_cols *h, size_t col) {
  size_t i = zsvsheet_hidden_cols_lower_bound(h, col);
  return i < h->count && h->cols[i] == col;
}

size_t zsvsheet_hidden_cols_in_range(const struct zsvsheet_hidden_cols *h, size_t from, size_t to) {
  // ascending, so the members in [from, to) are one run
  size_t i = zsvsheet_hidden_cols_lower_bound(h, from), n = 0;
  while (i + n < h->count && h->cols[i + n] < to)
    n++;
  return n;
}

size_t zsvsheet_hidden_cols_visible(const struct zsvsheet_hidden_cols *h, size_t col, size_t end) {
  while (col < end && zsvsheet_hidden_cols_has(h, col))
    col++;
  return col;
}

size_t zsvsheet_hidden_cols_visible_before(const struct zsvsheet_hidden_cols *h, size_t col) {
  while (col) {
    col--;
    if (!zsvsheet_hidden_cols_has(h, col))
      return col;
  }
  return (size_t)-1;
}

size_t zsvsheet_hidden_cols_visible_count(const struct zsvsheet_hidden_cols *h, size_t from, size_t to) {
  if (to <= from)
    return 0;
  return to - from - zsvsheet_hidden_cols_in_range(h, from, to);
}

// ASCII case fold; a byte >= 0x80 is its own fold, so multi-byte UTF-8 compares byte for
// byte -- the policy SQL's column-name match (COLLATE NOCASE) follows too
static char zsvsheet_column_name_fold(char c) {
  return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

int zsvsheet_column_name_matches(const char *pattern, const char *name) {
  // the '*' most recently passed is held with the name position it started at, so a
  // mismatch behind it retries with the star's run one character longer
  const char *star = NULL, *retry = NULL;
  while (*name) {
    if (*pattern == '*') {
      star = pattern++;
      retry = name;
    } else if (*pattern && zsvsheet_column_name_fold(*pattern) == zsvsheet_column_name_fold(*name)) {
      pattern++;
      name++;
    } else if (star) {
      pattern = star + 1;
      name = ++retry;
    } else
      return 0;
  }
  while (*pattern == '*')
    pattern++;
  return *pattern == '\0';
}
