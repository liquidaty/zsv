#ifndef ZSV_SHEET_BUFFER_H
#define ZSV_SHEET_BUFFER_H

#define ZSV_SHEET_BUFFER_DEFAULT_CELL_BUFF_LEN 16
#define ZSV_SHEET_BUFFER_DEFAULT_MAX_CELL_LEN 32768 - 1
#define ZSV_SHEET_BUFFER_DEFAULT_ROW_COUNT 1000

typedef struct zsv_sheet_buffer *zsv_sheet_buffer_t;

enum zsv_sheet_buffer_status {
  zsv_sheet_buffer_status_ok = 0,
  zsv_sheet_buffer_status_memory,
  zsv_sheet_buffer_status_error, // generic error
  zsv_sheet_buffer_status_utf8
};

struct zsv_sheet_buffer_opts {
  size_t cell_buff_len;  // default = 16. must be >= 2 * sizeof(void *)
  size_t max_cell_len;   // length in bytes; defaults to 32767
  size_t rows;           // rows to buffer. cannot be < 256
  char no_rownum_column; // reserved. TO DO: if set, omit row num column
};

zsv_sheet_buffer_t zsv_sheet_buffer_new(size_t cols, struct zsv_sheet_buffer_opts *opts,
                                        enum zsv_sheet_buffer_status *stat);

enum zsv_sheet_buffer_status zsv_sheet_buffer_write_cell(zsv_sheet_buffer_t buff, size_t row, size_t col,
                                                         const unsigned char *value);

enum zsv_sheet_buffer_status zsv_sheet_buffer_write_cell_w_len(zsv_sheet_buffer_t buff, size_t row, size_t col,
                                                               const unsigned char *value, size_t len);

const unsigned char *zsv_sheet_buffer_cell_display(zsv_sheet_buffer_t buff, size_t row, size_t col);

void zsv_sheet_buffer_delete(zsv_sheet_buffer_t);

size_t zsv_sheet_buffer_cols(zsv_sheet_buffer_t);
size_t zsv_sheet_buffer_rows(zsv_sheet_buffer_t buff);

#endif
