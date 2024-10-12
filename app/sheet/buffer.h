#ifndef ZSVSHEET_BUFFER_H
#define ZSVSHEET_BUFFER_H

#define ZSVSHEET_BUFFER_DEFAULT_CELL_BUFF_LEN 16
#define ZSVSHEET_BUFFER_DEFAULT_MAX_CELL_LEN 32768 - 1
#define ZSVSHEET_BUFFER_DEFAULT_ROW_COUNT 1000

typedef struct zsvsheet_buffer *zsvsheet_buffer_t;

struct zsvsheet_buffer_opts {
  size_t cell_buff_len;  // default = 16. must be >= 2 * sizeof(void *)
  size_t max_cell_len;   // length in bytes; defaults to 32767
  size_t rows;           // rows to buffer. cannot be < 256
  char no_rownum_column; // reserved. TO DO: if set, omit row num column
};

zsvsheet_buffer_t zsvsheet_buffer_new(size_t cols, struct zsvsheet_buffer_opts *opts, enum zsvsheet_status *stat);

enum zsvsheet_status zsvsheet_buffer_write_cell(zsvsheet_buffer_t buff, size_t row, size_t col,
                                                const unsigned char *value);

enum zsvsheet_status zsvsheet_buffer_write_cell_w_len(zsvsheet_buffer_t buff, size_t row, size_t col,
                                                      const unsigned char *value, size_t len);

const unsigned char *zsvsheet_buffer_cell_display(zsvsheet_buffer_t buff, size_t row, size_t col);

void zsvsheet_buffer_delete(zsvsheet_buffer_t);

size_t zsvsheet_buffer_cols(zsvsheet_buffer_t);
size_t zsvsheet_buffer_rows(zsvsheet_buffer_t buff);

#endif
