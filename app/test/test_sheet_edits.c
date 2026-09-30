/*
 * Unit test of the sheet cell-edit container (app/sheet/edits.c): entries stay sorted
 * by (row, col), a second write to a cell replaces it, and a cleared container is reusable
 */
#include <stdio.h>
#include <string.h>
#include "../sheet/edits.h"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int set_s(struct zsvsheet_edits *e, size_t row, size_t col, const char *s) {
  return zsvsheet_edits_set(e, row, col, (const unsigned char *)s, strlen(s));
}

int main(void) {
  struct zsvsheet_edits e = {0};
  CHECK(zsvsheet_edits_lower_bound(&e, 0, 0) == 0);

  // out-of-order inserts, enough to grow the array more than once
  for (size_t i = 0; i < 100; i++)
    CHECK(!set_s(&e, (i * 37) % 100, i % 3, "v"));
  CHECK(e.count == 100);
  for (size_t i = 1; i < e.count; i++) {
    const struct zsvsheet_cell_edit *a = &e.items[i - 1], *b = &e.items[i];
    CHECK(a->row < b->row || (a->row == b->row && a->col < b->col));
  }

  // a second value for a cell replaces the first
  CHECK(!set_s(&e, 37, 1, "new"));
  CHECK(e.count == 100);
  size_t k = zsvsheet_edits_lower_bound(&e, 37, 1);
  CHECK(e.items[k].row == 37 && e.items[k].col == 1 && e.items[k].len == 3 && !strcmp((char *)e.items[k].value, "new"));

  // lower bound: first entry at or after (row, col)
  CHECK(zsvsheet_edits_lower_bound(&e, 1000, 0) == e.count);
  k = zsvsheet_edits_lower_bound(&e, 50, 0);
  CHECK(e.items[k].row >= 50 && (k == 0 || e.items[k - 1].row < 50));

  // an empty value is a value
  CHECK(!zsvsheet_edits_set(&e, 5000, 0, (const unsigned char *)"", 0));
  CHECK(e.items[e.count - 1].len == 0 && e.items[e.count - 1].value[0] == '\0');

  zsvsheet_edits_clear(&e);
  CHECK(e.count == 0 && e.items == NULL);
  CHECK(!set_s(&e, 1, 1, "x")); // reusable after clear
  zsvsheet_edits_clear(&e);
  return 0;
}
