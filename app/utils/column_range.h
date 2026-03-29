#ifndef ZSV_COLUMN_RANGE_H
#define ZSV_COLUMN_RANGE_H

#include <stdio.h>
#include <string.h>

struct zsv_column_range {
  unsigned start; // 0-based
  unsigned count;
};

// Callback to look up a column name and return its 1-based index, or 0 if not found.
// start_after: skip columns with index <= this value (0 = search from beginning).
typedef unsigned (*zsv_column_name_lookup)(const char *name, size_t len, unsigned start_after, void *ctx);

// Callback to retrieve a column name given its 1-based index. Returns NULL if not found.
// Sets *len to the name length.
typedef const char *(*zsv_column_name_at)(unsigned col_1based, size_t *len, void *ctx);

// Parse one side of a column-range spec: "A-B", "A:B", or "A" (1-based).
// Returns 1 if range, 0 if single, -1 on error.
static int zsv_column_range_parse_side(const char *s, unsigned *start, unsigned *end) {
  if (sscanf(s, "%u-%u", start, end) == 2)
    return (*start > 0 && *end > 0 && *start <= *end) ? 1 : -1;
  if (sscanf(s, "%u:%u", start, end) == 2)
    return (*start > 0 && *end > 0 && *start <= *end) ? 1 : -1;
  if (sscanf(s, "%u", start) == 1) {
    *end = *start;
    return (*start > 0) ? 0 : -1;
  }
  return -1;
}

// Extended: also try column name lookup when numeric parse fails.
// start_after: for name lookup, skip columns with 1-based index <= this value.
static int zsv_column_range_parse_side_ex(const char *s, unsigned *start, unsigned *end,
                                           zsv_column_name_lookup lookup, void *ctx,
                                           unsigned start_after) {
  int result = zsv_column_range_parse_side(s, start, end);
  if (result >= 0)
    return result;
  if (lookup) {
    unsigned col = lookup(s, strlen(s), start_after, ctx);
    if (col > 0) {
      *start = col;
      *end = col;
      return 0; // single column
    }
  }
  return -1;
}

// Compute output ranges from parsed 1-based endpoints.
// Returns 0 on success, -1 on error.
static int zsv_column_range_compute(unsigned a1, unsigned a2, int a_type,
                                     unsigned b1, unsigned b2, int b_type,
                                     struct zsv_column_range *r1, struct zsv_column_range *r2) {
  if (a_type == 0 && b_type == 0) {
    // Both single: width = distance between them
    if (a1 == b1)
      return -1;
    unsigned lo = a1 < b1 ? a1 : b1;
    unsigned hi = a1 < b1 ? b1 : a1;
    a2 = a1 + (hi - lo) - 1;
    b2 = b1 + (hi - lo) - 1;
  } else if (a_type == 0) {
    a2 = a1 + (b2 - b1);
  } else if (b_type == 0) {
    b2 = b1 + (a2 - a1);
  }

  // Use the larger of the two range sizes
  size_t a_count = a2 - a1 + 1;
  size_t b_count = b2 - b1 + 1;
  size_t count = a_count > b_count ? a_count : b_count;
  a2 = a1 + (unsigned)count - 1;
  b2 = b1 + (unsigned)count - 1;

  // Reduce if ranges overlap
  unsigned lo_start = a1 < b1 ? a1 : b1;
  unsigned hi_start = a1 < b1 ? b1 : a1;
  size_t gap = hi_start - lo_start;
  if (count > gap)
    count = gap;
  if (count == 0)
    return -1;

  // Output 0-based ranges
  r1->start = a1 - 1;
  r1->count = (unsigned)count;
  r2->start = b1 - 1;
  r2->count = (unsigned)count;
  return 0;
}

// Extract and trim left/right sides around a separator position.
// left and right must be at least max_side bytes. Returns 0 on success.
static int zsv_column_range_split(const char *spec, const char *sep, size_t sep_len, size_t max_side,
                                   char *left, char *right) {
  size_t left_len = (size_t)(sep - spec);
  while (left_len > 0 && spec[left_len - 1] == ' ')
    left_len--;
  if (left_len == 0 || left_len >= max_side)
    return -1;

  const char *rstart = sep + sep_len;
  while (*rstart == ' ')
    rstart++;
  size_t right_len = strlen(rstart);
  while (right_len > 0 && rstart[right_len - 1] == ' ')
    right_len--;
  if (right_len == 0 || right_len >= max_side)
    return -1;

  memcpy(left, spec, left_len);
  left[left_len] = '\0';
  memcpy(right, rstart, right_len);
  right[right_len] = '\0';
  return 0;
}

// Parse a column-range spec into two 0-based ranges.
// Supports column names via optional lookup/name_at callbacks.
// Two sides separated by 'v' or 'vs' (case-insensitive, optional whitespace).
// Each side: A-B, A:B, just A (1-based columns), or a column name (when lookup != NULL).
// When both sides are single, width = distance between them.
// Mismatched widths expand to the larger. Overlap is trimmed.
// If name_at is provided, also supports single-column specs when the column name appears
// more than once (e.g. "ABC" or "3" when column 3's name has a duplicate).
static int zsv_column_range_parse_ex(const char *spec, struct zsv_column_range *r1, struct zsv_column_range *r2,
                                      zsv_column_name_lookup lookup, void *ctx,
                                      zsv_column_name_at name_at, void *name_at_ctx) {
  // When lookup is available, try all 'v'/'vs' positions (column names may contain 'v').
  // Without lookup, stop at the first 'v'/'vs' found.
  for (const char *p = spec; *p; p++) {
    if (*p != 'v' && *p != 'V')
      continue;

    // Try "vs" (2-char) first if applicable, then "v" (1-char)
    int max_tries = (p[1] == 's' || p[1] == 'S') ? 2 : 1;
    for (int try_i = 0; try_i < max_tries; try_i++) {
      size_t sl = (try_i == 0 && max_tries == 2) ? 2 : 1;
      char left[256], right[256];
      if (zsv_column_range_split(spec, p, sl, sizeof(left), left, right) != 0)
        continue;

      unsigned a1, a2, b1, b2;
      int a_type = zsv_column_range_parse_side_ex(left, &a1, &a2, lookup, ctx, 0);
      if (a_type < 0) {
        if (!lookup)
          return -1; // no name fallback; this is the only split point
        continue;
      }
      // For the right side, if same text as left, skip past the left match
      unsigned b_start_after = (strcmp(left, right) == 0) ? a1 : 0;
      int b_type = zsv_column_range_parse_side_ex(right, &b1, &b2, lookup, ctx, b_start_after);
      if (b_type < 0) {
        if (!lookup)
          return -1;
        continue;
      }

      if (zsv_column_range_compute(a1, a2, a_type, b1, b2, b_type, r1, r2) == 0)
        return 0;
    }

    if (!lookup)
      break; // without lookup, only the first 'v'/'vs' is considered
  }

  // No v/vs separator found (or none produced a valid parse).
  // Try single-column mode: if the column name has a duplicate,
  // treat it as "first occurrence vs second occurrence".
  if (lookup) {
    unsigned a1 = 0;
    const char *col_name = NULL;
    size_t col_name_len = 0;

    unsigned dummy_end;
    int rc = zsv_column_range_parse_side(spec, &a1, &dummy_end);
    if (rc == 0 && name_at) {
      // Single number: look up its column name to find a duplicate
      col_name = name_at(a1, &col_name_len, name_at_ctx ? name_at_ctx : ctx);
    } else if (rc < 0) {
      // Not a number: try as a column name
      unsigned col = lookup(spec, strlen(spec), 0, ctx);
      if (col > 0) {
        a1 = col;
        col_name = spec;
        col_name_len = strlen(spec);
      }
    }

    if (col_name && col_name_len > 0) {
      unsigned b1 = lookup(col_name, col_name_len, a1, ctx);
      if (b1 > 0)
        return zsv_column_range_compute(a1, a1, 0, b1, b1, 0, r1, r2);
    }
  }

  return -1;
}

#endif
