#ifndef ZSVSHEET_INTERNAL_H
#define ZSVSHEET_INTERNAL_H

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
