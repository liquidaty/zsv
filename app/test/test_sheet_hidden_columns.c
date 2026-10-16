/*
 * Unit test of the sheet hidden-column set and its column-name matcher
 * (app/sheet/hidden-columns.c): membership stays sorted and idempotent, range and
 * boundary queries hold at the ends, and the matcher folds ASCII case with '*' matching
 * any run
 */
#include <stdio.h>
#include <string.h>
#include "../sheet/hidden-columns.h"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int check_sorted(const struct zsvsheet_hidden_cols *h) {
  for (size_t i = 1; i < h->count; i++)
    if (!(h->cols[i - 1] < h->cols[i]))
      return 0;
  for (size_t i = 0; i < h->count; i++)
    if (!zsvsheet_hidden_cols_has(h, h->cols[i]) || !zsvsheet_hidden_cols_in_range(h, h->cols[i], h->cols[i] + 1))
      return 0;
  return 1;
}

int main(void) {
  struct zsvsheet_hidden_cols h = {0};
  CHECK(h.count == 0);
  CHECK(!zsvsheet_hidden_cols_has(&h, 0));
  CHECK(!zsvsheet_hidden_cols_in_range(&h, 0, 100));

  // out-of-order inserts, enough to grow the array more than once
  for (size_t i = 0; i < 100; i++)
    CHECK(!zsvsheet_hidden_cols_add(&h, (i * 37) % 100));
  CHECK(h.count == 100);
  CHECK(check_sorted(&h));

  // add is idempotent
  CHECK(!zsvsheet_hidden_cols_add(&h, 0));
  CHECK(!zsvsheet_hidden_cols_add(&h, 99));
  CHECK(h.count == 100);

  // range queries, at the ends and outside
  CHECK(zsvsheet_hidden_cols_in_range(&h, 0, 100) == 100);
  CHECK(zsvsheet_hidden_cols_in_range(&h, 0, 1) == 1);
  CHECK(zsvsheet_hidden_cols_in_range(&h, 99, 100) == 1);
  CHECK(zsvsheet_hidden_cols_in_range(&h, 0, 0) == 0);
  CHECK(zsvsheet_hidden_cols_in_range(&h, 50, 3) == 0); // empty (from past to)
  CHECK(zsvsheet_hidden_cols_in_range(&h, 100, (size_t)-1) == 0);
  CHECK(zsvsheet_hidden_cols_in_range(&h, 25, 75) == 50);

  // remove
  CHECK(zsvsheet_hidden_cols_remove(&h, 50) == 1);
  CHECK(!zsvsheet_hidden_cols_has(&h, 50));
  CHECK(zsvsheet_hidden_cols_remove(&h, 50) == 0); // already gone
  CHECK(zsvsheet_hidden_cols_remove(&h, 1000) == 0);
  CHECK(h.count == 99);
  CHECK(check_sorted(&h));
  for (size_t i = 0; i < 100; i++)
    zsvsheet_hidden_cols_remove(&h, i);
  CHECK(h.count == 0);

  zsvsheet_hidden_cols_clear(&h);
  CHECK(h.count == 0 && h.cols == NULL);
  CHECK(!zsvsheet_hidden_cols_add(&h, 7)); // reusable after clear
  CHECK(check_sorted(&h));
  zsvsheet_hidden_cols_clear(&h);

  // visible walks with {1, 2, 5} hidden out of [0, 8)
  CHECK(!zsvsheet_hidden_cols_add(&h, 1));
  CHECK(!zsvsheet_hidden_cols_add(&h, 2));
  CHECK(!zsvsheet_hidden_cols_add(&h, 5));
  CHECK(zsvsheet_hidden_cols_visible(&h, 0, 8) == 0);
  CHECK(zsvsheet_hidden_cols_visible(&h, 1, 8) == 3); // skips the run 1, 2
  CHECK(zsvsheet_hidden_cols_visible(&h, 2, 8) == 3);
  CHECK(zsvsheet_hidden_cols_visible(&h, 5, 8) == 6);
  CHECK(zsvsheet_hidden_cols_visible(&h, 6, 8) == 6);
  CHECK(zsvsheet_hidden_cols_visible(&h, 8, 8) == 8); // empty range
  CHECK(zsvsheet_hidden_cols_visible(&h, 1, 3) == 3); // every column in range is hidden
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 3) == 0);
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 6) == 4);
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 2) == 0);
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 1) == 0);
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 0) == (size_t)-1);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 0, 8) == 5);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 1, 3) == 0);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 0, 2) == 1);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 5, 3) == 0); // from past to
  zsvsheet_hidden_cols_clear(&h);

  // with the whole range hidden, a walk finds nothing in it
  for (size_t i = 0; i < 4; i++)
    CHECK(!zsvsheet_hidden_cols_add(&h, i));
  CHECK(zsvsheet_hidden_cols_visible(&h, 0, 4) == 4);
  CHECK(zsvsheet_hidden_cols_visible_before(&h, 4) == (size_t)-1);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 0, 4) == 0);
  CHECK(zsvsheet_hidden_cols_visible_count(&h, 4, 6) == 2);
  zsvsheet_hidden_cols_clear(&h);

  // the matcher: ASCII case-insensitive, a pattern without '*' matches a whole name
  CHECK(zsvsheet_column_name_matches("City", "City"));
  CHECK(zsvsheet_column_name_matches("city", "City"));
  CHECK(zsvsheet_column_name_matches("CITY", "city"));
  CHECK(zsvsheet_column_name_matches("", ""));
  CHECK(!zsvsheet_column_name_matches("", "City"));
  CHECK(!zsvsheet_column_name_matches("Cit", "City")); // no substring match
  CHECK(!zsvsheet_column_name_matches("City", "Cit"));
  CHECK(!zsvsheet_column_name_matches("Cita", "City"));

  // '*' matches any run, anywhere
  CHECK(zsvsheet_column_name_matches("Cit*", "City"));
  CHECK(zsvsheet_column_name_matches("*ity", "City"));
  CHECK(zsvsheet_column_name_matches("C*y", "City"));
  CHECK(zsvsheet_column_name_matches("*", "City"));
  CHECK(zsvsheet_column_name_matches("*", ""));
  CHECK(zsvsheet_column_name_matches("C*t*", "Country"));
  CHECK(zsvsheet_column_name_matches("*o*", "Country"));
  CHECK(zsvsheet_column_name_matches("**", "Country"));
  CHECK(zsvsheet_column_name_matches("***", ""));
  CHECK(zsvsheet_column_name_matches("*untr*", "COUNTRY"));
  CHECK(!zsvsheet_column_name_matches("*z*", "Country"));
  CHECK(!zsvsheet_column_name_matches("C*z*y", "Country"));
  CHECK(!zsvsheet_column_name_matches("*ity", "Cit"));
  CHECK(zsvsheet_column_name_matches("Cit*", "Cit"));
  CHECK(zsvsheet_column_name_matches("A*C", "AxxC"));
  CHECK(zsvsheet_column_name_matches("*.csv", "a.csv"));
  CHECK(!zsvsheet_column_name_matches("*.csv", "a.tsv"));

  // multi-byte UTF-8 compares byte for byte: ASCII case still folds, non-ASCII does not
  CHECK(zsvsheet_column_name_matches("caf\xc3\xa9", "CAF\xc3\xa9"));
  CHECK(!zsvsheet_column_name_matches("caf\xc3\x89", "CAF\xc3\xa9")); // É vs é
  CHECK(zsvsheet_column_name_matches("*\xc3\xa9", "caf\xc3\xa9"));

  return 0;
}
