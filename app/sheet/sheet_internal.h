#ifndef ZSVSHEET_INTERNAL_H
#define ZSVSHEET_INTERNAL_H

enum zsvsheet_priv_status {
  zsvsheet_priv_status_ok = 0,
  zsvsheet_priv_status_memory,
  zsvsheet_priv_status_error, // generic error
  zsvsheet_priv_status_utf8,
  zsvsheet_priv_status_continue // ignore / continue
  //  zsvsheet_priv_status_duplicate
};

struct zsvsheet_rowcol {
  size_t row;
  size_t col;
};

struct zsvsheet_input_dimensions {
  size_t col_count;
  size_t row_count;
};

struct zsvsheet_display_dimensions {
  size_t rows;
  size_t columns;
  size_t header_span;
  size_t footer_span;
};

#endif
