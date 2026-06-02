#ifndef ZSV_COMPARE_REDLINE_H
#define ZSV_COMPARE_REDLINE_H

#include <stdlib.h>

struct zsv_compare_redline_cell {
  unsigned char is_diff;      /* 1=diff array emitted, 0=scalar */
  unsigned char is_tolerated; /* 1=was within tolerance (only when !is_diff in default mode) */
  /* Scalar value (valid when !is_diff) */
  unsigned char *scalar_s;
  size_t scalar_len;
  /* Diff array parallel to inputs[] (valid when is_diff) */
  unsigned char **diff_s; /* [input_count] — NULL slot means that input lacks the cell */
  size_t *diff_len;       /* [input_count] */
};

struct zsv_compare_redline_row {
  struct zsv_compare_redline_row *next;
  unsigned char is_object; /* 1=object form with missing_in */
  unsigned *missing_in;    /* [missing_in_count] input indices */
  unsigned missing_in_count;
  struct zsv_compare_redline_cell *cells; /* [output_colcount] */
};

struct zsv_compare_col_stat {
  size_t compared;
  size_t matched;
  size_t within_tolerance;
  size_t differing;
};

struct zsv_compare_redline {
  struct zsv_compare_redline_row *rows_head;
  struct zsv_compare_redline_row **rows_tail;

  size_t *input_row_counts;               /* [input_count] */
  struct zsv_compare_col_stat *col_stats; /* [output_colcount] */

  size_t rows_in_all;
  size_t *rows_only_in_input; /* [input_count] */
  size_t rows_with_diff;
  size_t cells_compared;
  size_t cells_matched;
  size_t cells_within_tolerance;
  size_t cells_differing;
};

#endif
