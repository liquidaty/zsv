#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <zsv/utils/prop.h>
#include "sheet_internal.h"
#include "screen_buffer.h"
#include "../utils/index.h"
#include "index.h"

#if defined(WIN32) || defined(_WIN32)
#ifndef NO_MEMMEM
#define NO_MEMMEM
#endif
#include <zsv/utils/memmem.h>
#endif
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>

// zsvsheet_found_in_row: return 0 if not found, else 1-based index of column
static size_t zsvsheet_found_in_row(zsv_parser parser, size_t col_start, size_t col_count, const char *target,
                                    size_t target_len, size_t specified_column_plus_1,
                                    char find_exact // not yet implemented
) {
  if (col_start >= col_count)
    return 0;

  struct zsv_cell first_cell = zsv_get_cell(parser, col_start);
  struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);

  if (memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, target, target_len)) {
    for (size_t i = col_start; i < col_count; i++) {
      if (specified_column_plus_1 == 0 || i + 1 == specified_column_plus_1) {
        struct zsv_cell c = zsv_get_cell(parser, i);
        if (find_exact) {
          if (c.len == target_len && !memcmp(c.str, target, c.len))
            return i + 1;
        } else if (memmem(c.str, c.len, target, target_len))
          return i + 1;
      }
    }
  }
  return 0;
}

static void *get_data_index(void *d);

static void get_data_index_async(struct zsvsheet_ui_buffer *uibuffp, const char *filename, struct zsv_opts *optsp,
                                 struct zsv_prop_handler *custom_prop_handler, char *old_ui_status) {
  struct zsvsheet_index_opts *ixopts = calloc(1, sizeof(*ixopts));
  ixopts->mutexp = &uibuffp->mutex;
  ixopts->filename = filename;
  ixopts->zsv_opts = *optsp;
  ixopts->custom_prop_handler = custom_prop_handler;
  ixopts->uib = uibuffp;
  ixopts->uib->ixopts = ixopts;
  ixopts->old_ui_status = old_ui_status;

  if (uibuffp->worker_active)
    zsvsheet_ui_buffer_join_worker(uibuffp);
  zsvsheet_ui_buffer_create_worker(uibuffp, get_data_index, ixopts);
}

static int read_data(struct zsvsheet_ui_buffer **uibufferp,   // a new zsvsheet_ui_buffer will be allocated
                     struct zsvsheet_ui_buffer_opts *uibopts, // if *uibufferp == NULL and uibopts != NULL
                     size_t start_row, size_t start_col, size_t header_span, struct zsvsheet_opts *zsvsheet_opts,
                     struct zsv_prop_handler *custom_prop_handler) {
  int rc = 0;
  struct uib_parse_errs parse_errs;
  uib_parse_errs_init(&parse_errs, 100);
  FILE *fp = NULL;
  const char *filename = (uibufferp && *uibufferp) ? (*uibufferp)->filename : uibopts ? uibopts->filename : NULL;
  struct zsv_opts opts = {0};
  if (uibufferp && *uibufferp)
    opts = (*uibufferp)->zsv_opts;
  else if (uibopts)
    opts = uibopts->zsv_opts;

  struct zsvsheet_ui_buffer *uibuff = uibufferp ? *uibufferp : NULL;
  size_t remaining_rows_to_skip = start_row;
  size_t remaining_header_to_skip = header_span;
  size_t original_row_num = 0;

  if (uibuff) {
    if (uibuff->data_filename)
      filename = uibuff->data_filename;
    else if (uibuff->filename)
      filename = uibuff->filename;
  }

  if (!filename && uibopts) {
    if (uibopts->data_filename)
      filename = uibopts->data_filename;
    else if (uibopts->filename)
      filename = uibopts->filename;
  }

  assert(filename != NULL);
  fp = fopen(filename, "rb");
  if (!fp) {
    rc = errno;
    goto done;
  }

  opts.stream = fp; // Input file stream

  opts.errprintf = uib_parse_errs_printf;
  if (uibuff)
    opts.errf = &uibuff->parse_errs;
  else
    opts.errf = &parse_errs;

  zsv_parser parser = {0};
  if (zsv_new_with_properties(&opts, custom_prop_handler, filename, &parser) != zsv_status_ok) {
    rc = errno ? errno : -1;
    goto done;
  }

  // clear error logging before reusing opts
  opts.errprintf = zsv_no_printf;
  if (uibuff) {
    pthread_mutex_lock(&uibuff->mutex);

    enum zsv_index_status zst = zsv_index_status_ok;
    if (zsvsheet_ui_buffer_index_ready(uibuff, 1)) {
      opts.header_span = 0;
      opts.rows_to_ignore = 0;

      zst = zsv_index_seek_row(uibuff->index, &opts, start_row);

      zsv_delete(parser);
      parser = zsv_new(&opts);

      remaining_header_to_skip = 0;
      remaining_rows_to_skip = 0;
      original_row_num = header_span + start_row;
    }
    pthread_mutex_unlock(&uibuff->mutex);
    if (zst != zsv_index_status_ok) {
      rc = errno ? errno : -1;
      goto done;
    }
  }

  size_t rows_read = header_span;

  size_t find_len = zsvsheet_opts->find ? strlen(zsvsheet_opts->find) : 0;
  size_t rows_searched = 0;
  zsvsheet_screen_buffer_t buffer = uibuff ? uibuff->buffer : NULL;
  if (uibuff && uibuff->has_row_num)
    zsvsheet_opts->hide_row_nums = 1;

  while (zsv_next_row(parser) == zsv_status_row &&
         (rows_read == 0 || rows_read < zsvsheet_screen_buffer_rows(buffer))) { // for each row

    size_t col_count = zsv_cell_count(parser);
    if (uibuff == NULL && uibufferp && uibopts && col_count > 0) {
      enum zsvsheet_priv_status stat;
      struct zsvsheet_ui_buffer *tmp_uibuff = NULL;
      if (!(buffer = zsvsheet_screen_buffer_new(col_count, uibopts->buff_opts, &stat)) ||
          stat != zsvsheet_priv_status_ok || !(tmp_uibuff = zsvsheet_ui_buffer_new(buffer, uibopts))) {
        if (tmp_uibuff)
          zsvsheet_ui_buffer_delete(tmp_uibuff);
        else
          zsvsheet_screen_buffer_delete(buffer);
        rc = -1;
        goto done;
      }
      *uibufferp = uibuff = tmp_uibuff;
      if (uibuff) {
        uibuff->parse_errs = parse_errs;            // transfer errors
        memset(&parse_errs, 0, sizeof(parse_errs)); // prevent double-free
      }
    }

    // row number
    size_t rownum_column_offset = 0;
    if (rows_read == 0 && zsvsheet_opts->hide_row_nums == 0) {
      // Check if we already have Row #
      // TO DO: instead of checking the header, use (*uibufferp)->has_row_num
      struct zsv_cell c = zsv_get_cell(parser, 0);
      if (c.len == ZSVSHEET_ROWNUM_HEADER_LEN && !memcmp(c.str, ZSVSHEET_ROWNUM_HEADER, c.len)) {
        zsvsheet_opts->hide_row_nums = 1;
        if (uibuff)
          uibuff->has_row_num = 1;
      }
    }

    original_row_num++;
    if (remaining_header_to_skip > 0) {
      remaining_header_to_skip--;
      continue;
    }
    if (uibuff) {
      if (col_count + !buffer->opts.no_rownum_column > buffer->cols) {
        if (zsvsheet_screen_buffer_grow(buffer, col_count) != zsvsheet_priv_status_ok) {
          rc = -1;
          goto done;
        }
      }
      if (col_count > uibuff->dimensions.col_count)
        uibuff->dimensions.col_count = col_count;
    }

    if (remaining_rows_to_skip > 0) {
      remaining_rows_to_skip--;
      continue;
    }

    if (zsvsheet_opts->find) { // find the next occurrence
      rows_searched++;
      size_t colIndexPlus1 =
        zsvsheet_found_in_row(parser, zsvsheet_opts->found_colnum, col_count, zsvsheet_opts->find, find_len,
                              zsvsheet_opts->find_specified_column_plus_1, zsvsheet_opts->find_exact);
      if (colIndexPlus1) {
        zsvsheet_opts->found_rownum = rows_searched + start_row;
        zsvsheet_opts->found_colnum = colIndexPlus1 - 1;
        break;
      } else
        zsvsheet_opts->found_colnum = 0; // next row search starts at beg of row
      continue;
    }

    if (zsvsheet_opts->hide_row_nums == 0) {
      if (rows_read == 0) // header
        zsvsheet_screen_buffer_write_cell(buffer, 0, 0, (const unsigned char *)ZSVSHEET_ROWNUM_HEADER);
      else {
        char buff[32];
        int n = snprintf(buff, sizeof(buff), "%zu", original_row_num - 1);
        if (!(n > 0 && n < (int)sizeof(buff)))
          sprintf(buff, "########");
        zsvsheet_screen_buffer_write_cell(buffer, rows_read, 0, (unsigned char *)buff);
      }
      rownum_column_offset = 1;
    }

    for (size_t i = start_col; i < col_count && i + rownum_column_offset < zsvsheet_screen_buffer_cols(buffer); i++) {
      struct zsv_cell c = zsv_get_cell(parser, i);
      if (c.len)
        zsvsheet_screen_buffer_write_cell_w_len(buffer, rows_read, i + rownum_column_offset, c.str, c.len);
    }

    rows_read++;
  }
  if (!uibuff) {
    rc = 0;
    goto done;
  }

  pthread_mutex_lock(&uibuff->mutex);
  char need_index = !uibuff->index_started && !uibuff->write_in_progress;
  char *old_ui_status = uibuff->status;
  if (need_index)
    uibuff->index_started = 1;
  pthread_mutex_unlock(&uibuff->mutex);

  if (need_index) {
    if (asprintf(&uibuff->status, "%s(building index) ", old_ui_status ? old_ui_status : "") == -1) {
      rc = -1;
      goto done;
    }

    uibuff->buff_used_rows = rows_read;
    uibuff->dimensions.row_count = rows_read;
    if (original_row_num > 1 && rows_read > 0) {
      opts.stream = NULL;
      get_data_index_async(uibuff, filename, &opts, custom_prop_handler, old_ui_status);
    }
  } else if (rows_read > uibuff->buff_used_rows) {
    uibuff->buff_used_rows = rows_read;
    uibuff->dimensions.row_count = rows_read;
  }

done:
  uib_parse_errs_clear(&parse_errs);
  if (fp)
    fclose(fp);
  zsv_delete(parser);
  return rc;
}

// get_data_index(): return an index for constant-time access
static void *get_data_index(void *gdi) {
  struct zsvsheet_index_opts *d = gdi;
  pthread_mutex_t *mutexp = d->mutexp;
  int *errp = d->errp;
  struct zsvsheet_ui_buffer *uib = d->uib;
  char *ui_status = uib->status;

  enum zsv_index_status ix_status = build_memory_index(d);

  if (ix_status != zsv_index_status_ok) {
    pthread_mutex_lock(mutexp);
    if (errp != NULL)
      *errp = errno;
    free(d);
    pthread_mutex_unlock(mutexp);
    return NULL;
  }

  pthread_mutex_lock(mutexp);
  uib->status = d->old_ui_status;
  uib->ixopts = NULL;
  pthread_mutex_unlock(mutexp);

  free(ui_status);
  free(d);

  return NULL;
}

static size_t zsvsheet_find_next(struct zsvsheet_ui_buffer *uib, struct zsvsheet_opts *zsvsheet_opts,
                                 size_t header_span, struct zsv_prop_handler *custom_prop_handler) {
  struct zsvsheet_rowcol *input_offset = &uib->input_offset;
  struct zsvsheet_rowcol *buff_offset = &uib->buff_offset;
  size_t cursor_row = uib->cursor_row;
  size_t start_row = input_offset->row + buff_offset->row + header_span + cursor_row - 1;
  if (start_row > 0)
    start_row--;
  read_data(&uib, NULL, start_row, 0, header_span, zsvsheet_opts, custom_prop_handler);
  zsvsheet_opts->find = NULL;
  return zsvsheet_opts->found_rownum;
}
