#include "screen_buffer.h"

#include <zsv/ext.h> // zsvsheet_cell_attr_t

struct zsvsheet_screen_buffer {
  size_t cols;
  size_t long_cell_count;
  struct zsvsheet_screen_buffer_opts opts;
  unsigned char *data;
  zsvsheet_cell_attr_t *cell_attrs; // used for per-cell attron() and attroff()
  // to do: add hooks for extension
};

static inline size_t buffer_data_offset(zsvsheet_screen_buffer_t buff, size_t row, size_t col) {
  assert(row < buff->opts.rows && col < buff->cols);
  return row * buff->cols * buff->opts.cell_buff_len + col * buff->opts.cell_buff_len;
}

static void set_long_cell(zsvsheet_screen_buffer_t buff, size_t offset, char *heap) {
  char **target = (char **)(buff->data + offset);
  *target = heap;
  // set flag indicating that this is long cell
  *(buff->data + offset + buff->opts.cell_buff_len - 1) = (char)1;
}

static char *get_long_cell(zsvsheet_screen_buffer_t buff, size_t offset) {
  char **valuep = (char **)(buff->data + offset);
  if (valuep)
    return *valuep;
  return NULL;
}

static inline int is_long_cell(const unsigned char *mem, size_t cell_buff_len) {
  return *(mem + cell_buff_len - 1) != '\0';
}

size_t zsvsheet_screen_buffer_cols(zsvsheet_screen_buffer_t buff) {
  return buff->cols;
}

size_t zsvsheet_screen_buffer_rows(zsvsheet_screen_buffer_t buff) {
  return buff->opts.rows;
}

static void free_long_cell(zsvsheet_screen_buffer_t buff, size_t offset) {
  if (is_long_cell(buff->data + offset, buff->opts.cell_buff_len)) {
    char *value_copy = get_long_cell(buff, offset);
    free(value_copy);
    memset(buff->data + offset, 0, buff->opts.cell_buff_len);
    buff->long_cell_count--;
  }
}

void zsvsheet_screen_buffer_delete(zsvsheet_screen_buffer_t buff) {
  if (buff) {
    for (size_t i = 0; i < buff->opts.rows && buff->long_cell_count > 0; i++) {
      for (size_t j = 0; j < buff->cols && buff->long_cell_count > 0; j++) {
        size_t offset = buffer_data_offset(buff, i, j);
        free_long_cell(buff, offset);
      }
    }
    free(buff->cell_attrs);
    free(buff->data);
    free(buff);
  }
}

zsvsheet_screen_buffer_t zsvsheet_screen_buffer_new(size_t cols, struct zsvsheet_screen_buffer_opts *opts,
                                                    enum zsvsheet_priv_status *stat) {
  struct zsvsheet_screen_buffer_opts bopts = {0};
  if (!opts)
    opts = &bopts;
  *stat = zsvsheet_priv_status_ok;
  if (opts->rows == 0)
    opts->rows = ZSVSHEET_SCREEN_BUFFER_DEFAULT_ROW_COUNT;
  else if (opts->rows < 256)
    opts->rows = 256;
  if (opts->cell_buff_len == 0)
    opts->cell_buff_len = ZSVSHEET_SCREEN_BUFFER_DEFAULT_CELL_BUFF_LEN;
  if (opts->max_cell_len == 0)
    opts->max_cell_len = ZSVSHEET_SCREEN_BUFFER_DEFAULT_MAX_CELL_LEN;
  if (opts->cell_buff_len < sizeof(void *) * 2)
    *stat = zsvsheet_priv_status_error;
  else {
    if (!opts->no_rownum_column)
      cols++;
    void *data = calloc(opts->rows, cols * opts->cell_buff_len);
    if (!data)
      *stat = zsvsheet_priv_status_memory;
    else {
      struct zsvsheet_screen_buffer *buff = calloc(1, sizeof(*buff));
      if (!buff)
        *stat = zsvsheet_priv_status_memory;
      else {
        buff->opts.rows = opts->rows;
        buff->cols = cols;
        buff->data = data;
        buff->opts = *opts;
        return buff;
      }
    }
    free(data);
  }
  return NULL;
}

enum zsvsheet_priv_status zsvsheet_screen_buffer_grow(zsvsheet_screen_buffer_t buff, size_t cols) {
  size_t old_cols = buff->cols;
  size_t rows = buff->opts.rows;
  size_t cell_buff_len = buff->opts.cell_buff_len;
  size_t old_row_len = old_cols * cell_buff_len;

  if (!buff->opts.no_rownum_column)
    cols++;

  size_t row_len = cols * cell_buff_len;

  assert(cols > old_cols);

  void *old_data = buff->data;
  void *data = calloc(rows, cols * cell_buff_len);
  if (!data)
    return zsvsheet_priv_status_memory;

  for (size_t i = 0; i < rows; i++) {
    size_t old_row_off = i * old_row_len;
    size_t row_off = i * row_len;

    memcpy((char *)data + row_off, (char *)old_data + old_row_off, old_row_len);
  }

  buff->data = data;
  buff->cols = cols;

  free(old_data);

  return zsvsheet_priv_status_ok;
}

#ifndef UTF8_NOT_FIRST_CHAR
#define UTF8_NOT_FIRST_CHAR(x) ((x & 0xC0) == 0x80)
#endif

enum zsvsheet_priv_status zsvsheet_screen_buffer_write_cell_w_len(zsvsheet_screen_buffer_t buff, size_t row, size_t col,
                                                                  const unsigned char *value, size_t len) {
  enum zsvsheet_priv_status stat = zsvsheet_priv_status_ok;
  size_t offset = buffer_data_offset(buff, row, col);
  free_long_cell(buff, offset);
  if (len < buff->opts.cell_buff_len) {
    if (len)
      // copy into fixed-size buff
      memcpy(buff->data + offset, value, len);
    *(buff->data + offset + len) = '\0';
  } else {
    // we have a long cell
    if (len > buff->opts.max_cell_len) {
      len = buff->opts.max_cell_len;
      while (len > 0 && value[len] >= 128 && UTF8_NOT_FIRST_CHAR(value[len]))
        // we are in the middle of a multibyte char, so back up
        len--;
    }
    if (!len) // the only reason len could be 0 is if our input was not valid utf8, but check to make sure anyway
      stat = zsvsheet_priv_status_utf8;
    else {
      char *value_copy = malloc(1 + len);
      if (value_copy) {
        memcpy(value_copy, value, len);
        value_copy[len] = '\0';
        set_long_cell(buff, offset, value_copy);
        buff->long_cell_count++;
      } else
        stat = zsvsheet_priv_status_memory;
    }
  }
  return stat;
}

enum zsvsheet_priv_status zsvsheet_screen_buffer_write_cell(zsvsheet_screen_buffer_t buff, size_t row, size_t col,
                                                            const unsigned char *value) {
  return zsvsheet_screen_buffer_write_cell_w_len(buff, row, col, value, strlen((void *)value));
}

int zsvsheet_screen_buffer_cell_attrs(zsvsheet_screen_buffer_t buff, size_t row, size_t col) {
  if (buff->cell_attrs) {
    size_t offset = row * buff->cols + col;
    return buff->cell_attrs[offset];
  }
  return 0;
}

const unsigned char *zsvsheet_screen_buffer_cell_display(zsvsheet_screen_buffer_t buff, size_t row, size_t col) {
  if (row < buff->opts.rows && col < buff->cols) {
    size_t offset = row * buff->cols * buff->opts.cell_buff_len + col * buff->opts.cell_buff_len;
    const unsigned char *cell = &buff->data[offset];
    if (!is_long_cell(cell, buff->opts.cell_buff_len))
      return cell;

    // if the fixed-length cell memory does not end with NULL,
    // then the cell value is a pointer to memory holding the value
    return (unsigned char *)get_long_cell(buff, offset);
  }
  return NULL;
}
