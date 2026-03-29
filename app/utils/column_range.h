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

// Parse a column-range spec into two 0-based ranges.
// Two sides separated by 'v' or 'vs' (case-insensitive, optional whitespace).
// Each side: A-B, A:B, or just A (1-based columns).
// When both sides are single, width = distance between them.
// Mismatched widths expand to the larger. Overlap is trimmed.
static int zsv_column_range_parse(const char *spec, struct zsv_column_range *r1, struct zsv_column_range *r2) {
  // Find 'v' or 'vs' separator
  const char *sep = NULL;
  size_t sep_len = 0;
  for (const char *p = spec; *p; p++) {
    if (*p == 'v' || *p == 'V') {
      sep = p;
      sep_len = (p[1] == 's' || p[1] == 'S') ? 2 : 1;
      break;
    }
  }
  if (!sep)
    return -1;

  // Extract and trim left side
  size_t left_len = sep - spec;
  while (left_len > 0 && spec[left_len - 1] == ' ')
    left_len--;
  if (left_len == 0 || left_len >= 64)
    return -1;
  char left[64];
  memcpy(left, spec, left_len);
  left[left_len] = '\0';

  // Extract and trim right side
  const char *rstart = sep + sep_len;
  while (*rstart == ' ')
    rstart++;
  size_t right_len = strlen(rstart);
  while (right_len > 0 && rstart[right_len - 1] == ' ')
    right_len--;
  if (right_len == 0 || right_len >= 64)
    return -1;
  char right[64];
  memcpy(right, rstart, right_len);
  right[right_len] = '\0';

  // Parse each side
  unsigned a1, a2, b1, b2;
  int a_type = zsv_column_range_parse_side(left, &a1, &a2);
  int b_type = zsv_column_range_parse_side(right, &b1, &b2);
  if (a_type < 0 || b_type < 0)
    return -1;

  return zsv_column_range_compute(a1, a2, a_type, b1, b2, b_type, r1, r2);
}

// Extended parse that supports column names via a lookup callback.
// Tries all possible 'v'/'vs' split positions to handle column names containing 'v'.
static int zsv_column_range_parse_ex(const char *spec, struct zsv_column_range *r1, struct zsv_column_range *r2,
                                      zsv_column_name_lookup lookup, void *ctx) {
  if (!lookup)
    return zsv_column_range_parse(spec, r1, r2);

  for (const char *p = spec; *p; p++) {
    if (*p != 'v' && *p != 'V')
      continue;

    // Try "vs" (2-char) first if applicable, then "v" (1-char)
    int max_tries = (p[1] == 's' || p[1] == 'S') ? 2 : 1;
    for (int try_i = 0; try_i < max_tries; try_i++) {
      size_t sl = (try_i == 0 && max_tries == 2) ? 2 : 1;

      // Extract and trim left side
      size_t left_len = (size_t)(p - spec);
      while (left_len > 0 && spec[left_len - 1] == ' ')
        left_len--;
      if (left_len == 0 || left_len >= 256)
        continue;

      // Extract and trim right side
      const char *rstart = p + sl;
      while (*rstart == ' ')
        rstart++;
      size_t right_len = strlen(rstart);
      while (right_len > 0 && rstart[right_len - 1] == ' ')
        right_len--;
      if (right_len == 0 || right_len >= 256)
        continue;

      char left[256], right[256];
      memcpy(left, spec, left_len);
      left[left_len] = '\0';
      memcpy(right, rstart, right_len);
      right[right_len] = '\0';

      unsigned a1, a2, b1, b2;
      int a_type = zsv_column_range_parse_side_ex(left, &a1, &a2, lookup, ctx, 0);
      if (a_type < 0)
        continue;
      // For the right side, if same name as left, skip past the left match
      unsigned b_start_after = (strcmp(left, right) == 0) ? a1 : 0;
      int b_type = zsv_column_range_parse_side_ex(right, &b1, &b2, lookup, ctx, b_start_after);
      if (b_type < 0)
        continue;

      if (zsv_column_range_compute(a1, a2, a_type, b1, b2, b_type, r1, r2) == 0)
        return 0;
    }
  }
  return -1;
}

#endif
