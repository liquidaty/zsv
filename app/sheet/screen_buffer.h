#ifndef ZSVSHEET_SCREEN_BUFFER_H
#define ZSVSHEET_SCREEN_BUFFER_H

#define ZSVSHEET_SCREEN_BUFFER_DEFAULT_CELL_BUFF_LEN 16
#define ZSVSHEET_SCREEN_BUFFER_DEFAULT_MAX_CELL_LEN 32768 - 1
#define ZSVSHEET_SCREEN_BUFFER_DEFAULT_ROW_COUNT 1024

typedef struct zsvsheet_screen_buffer *zsvsheet_screen_buffer_t;

struct zsvsheet_screen_buffer_opts {
  size_t cell_buff_len;  // default = 16. must be >= 2 * sizeof(void *)
  size_t max_cell_len;   // length in bytes; defaults to 32767
  size_t rows;           // rows to buffer. cannot be < 256
  char no_rownum_column; // reserved. TO DO: if set, omit row num column
};

zsvsheet_screen_buffer_t zsvsheet_screen_buffer_new(size_t cols, struct zsvsheet_screen_buffer_opts *opts,
                                                    enum zsvsheet_priv_status *stat);
enum zsvsheet_priv_status zsvsheet_screen_buffer_grow(zsvsheet_screen_buffer_t buff, size_t cols);

enum zsvsheet_priv_status zsvsheet_screen_buffer_write_cell(zsvsheet_screen_buffer_t buff, size_t row, size_t col,
                                                            const unsigned char *value);

enum zsvsheet_priv_status zsvsheet_screen_buffer_write_cell_w_len(zsvsheet_screen_buffer_t buff, size_t row, size_t col,
                                                                  const unsigned char *value, size_t len);

const unsigned char *zsvsheet_screen_buffer_cell_display(zsvsheet_screen_buffer_t buff, size_t row, size_t col);

int zsvsheet_screen_buffer_cell_attrs(zsvsheet_screen_buffer_t buff, size_t row, size_t col);

void zsvsheet_screen_buffer_delete(zsvsheet_screen_buffer_t);

size_t zsvsheet_screen_buffer_cols(zsvsheet_screen_buffer_t);
size_t zsvsheet_screen_buffer_rows(zsvsheet_screen_buffer_t buff);

#endif
