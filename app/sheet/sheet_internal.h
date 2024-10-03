#ifndef ZTV_H
#define ZTV_H

struct ztv_rowcol {
  size_t row;
  size_t col;
};

struct input_dimensions {
  size_t col_count;
  size_t row_count;
  void *index;
};

struct display_dims {
  size_t rows;
  size_t columns;
  size_t header_span;
  size_t footer_span;
};

#endif
