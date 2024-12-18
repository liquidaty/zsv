#ifndef ZSVSHEET_BUFFER_INFO_H
#define ZSVSHEET_BUFFER_INFO_H

#include <stddef.h>
#include "ui_buffer.h"

// Public buffer info structure
struct zsvsheet_buffer_info {
  int has_row_num;
  int rownum_col_offset;
};

#endif /* ZSVSHEET_BUFFER_INFO_H */
