#ifndef ZSVSHEET_HIDDEN_COLUMNS_H
#define ZSVSHEET_HIDDEN_COLUMNS_H

#include <stddef.h>

// A buffer's hidden columns: the screen-buffer column indexes whose cells the display
// skips, ascending, one entry per column. Hiding is a view setting: the screen buffer
// keeps every column and the data behind it is untouched. The row-number column is
// not a member; callers never add it
struct zsvsheet_hidden_cols {
  size_t *cols;
  size_t count;
  size_t allocated;
};

// Add col to the set. Returns 0 (also when it is already a member), or ENOMEM, in which
// case the set is unchanged
int zsvsheet_hidden_cols_add(struct zsvsheet_hidden_cols *h, size_t col);

// Remove col if it is a member. Returns 1 if it was, else 0
int zsvsheet_hidden_cols_remove(struct zsvsheet_hidden_cols *h, size_t col);

// Free the set; `h` is empty and reusable afterwards
void zsvsheet_hidden_cols_clear(struct zsvsheet_hidden_cols *h);

int zsvsheet_hidden_cols_has(const struct zsvsheet_hidden_cols *h, size_t col);

// How many members fall in [from, to)
size_t zsvsheet_hidden_cols_in_range(const struct zsvsheet_hidden_cols *h, size_t from, size_t to);

/* Walks over the columns a reader can still see, for a caller that keeps an index
 * elsewhere: the display windows, the cursor and the column searches all move through
 * these. `end` bounds the columns under consideration (one past the last) */

// First column in [col, end) that is not a member, else end
size_t zsvsheet_hidden_cols_visible(const struct zsvsheet_hidden_cols *h, size_t col, size_t end);

// Last column before col that is not a member, else (size_t)-1
size_t zsvsheet_hidden_cols_visible_before(const struct zsvsheet_hidden_cols *h, size_t col);

// How many columns in [from, to) are not members
size_t zsvsheet_hidden_cols_visible_count(const struct zsvsheet_hidden_cols *h, size_t from, size_t to);

// Whether a column name matches a pattern: ASCII case-insensitive, '*' matches any run
// of characters (including none). A pattern without '*' matches a whole name only
int zsvsheet_column_name_matches(const char *pattern, const char *name);

#endif
