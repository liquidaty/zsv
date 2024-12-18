#ifndef ZSVSHEET_BUFFER_INFO_H
#define ZSVSHEET_BUFFER_INFO_H

#include "ui_buffer.h"

// Forward declarations
struct zsvsheet_ui_buffer;
typedef struct zsvsheet_ui_buffer *zsvsheet_buffer_t;

// Public buffer info structure
struct zsvsheet_buffer_info {
  int has_row_num;
  int rownum_col_offset;
};

// Internal buffer info structure with additional fields
struct zsvsheet_buffer_info_internal {
  int has_row_num;
  int rownum_col_offset;
  unsigned char flags;
  struct zsvsheet_input_dimensions dimensions;  // Match the type used in ui_buffer.h
};

#endif /* ZSVSHEET_BUFFER_INFO_H */
