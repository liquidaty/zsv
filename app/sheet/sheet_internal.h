#ifndef ZSVSHEET_INTERNAL_H
#define ZSVSHEET_INTERNAL_H

enum zsvsheet_status {
  zsvsheet_status_ok = 0,
  zsvsheet_status_memory,
  zsvsheet_status_error, // generic error
  zsvsheet_status_utf8,
  zsvsheet_status_continue // ignore / continue
  //  zsvsheet_status_duplicate
};

struct zsvsheet_rowcol {
  size_t row;
  size_t col;
};

struct zsvsheet_input_dimensions {
  size_t col_count;
  size_t row_count;
  void *index;
};

struct zsvsheet_display_dimensions {
  size_t rows;
  size_t columns;
  size_t header_span;
  size_t footer_span;
};

#endif
