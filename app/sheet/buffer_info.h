#ifndef ZSVSHEET_BUFFER_INFO_H
#define ZSVSHEET_BUFFER_INFO_H

#include <stddef.h>

// Forward declarations
struct zsvsheet_ui_buffer;
typedef struct zsvsheet_ui_buffer *zsvsheet_buffer_t;

// Public buffer info structure
struct zsvsheet_buffer_info {
  int has_row_num;
  int rownum_col_offset;
};

#endif /* ZSVSHEET_BUFFER_INFO_H */
