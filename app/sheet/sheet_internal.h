#ifndef ZSVSHEET_INTERNAL_H
#define ZSVSHEET_INTERNAL_H

#define ZSVSHEET_ROWNUM_HEADER "Row #"
#define ZSVSHEET_ROWNUM_HEADER_LEN strlen(ZSVSHEET_ROWNUM_HEADER)

enum zsvsheet_priv_status {
  zsvsheet_priv_status_ok = 0,
  zsvsheet_priv_status_memory,
  zsvsheet_priv_status_error, // generic error
  zsvsheet_priv_status_utf8,
  zsvsheet_priv_status_continue // ignore / continue
  //  zsvsheet_priv_status_duplicate
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

struct zsvsheet_buffer_info_internal {
  unsigned char index_started : 1;
  unsigned char index_ready : 1;
  unsigned char write_in_progress : 1;
  unsigned char write_done : 1;
  unsigned char _ : 4;
};

#endif
