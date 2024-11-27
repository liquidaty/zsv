#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/index.h>
#include "sheet_internal.h"
#include "screen_buffer.h"
#include "index.h"

#if defined(WIN32) || defined(_WIN32)
#define NO_MEMMEM
#include <zsv/utils/memmem.h>
#endif
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>

static char *zsvsheet_found_in_row(zsv_parser parser, size_t col_count, const char *target, size_t target_len) {
  if (col_count == 0)
    return NULL;

  struct zsv_cell first_cell = zsv_get_cell(parser, 0);
  struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);

  return memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, target, target_len);
}

static void *get_data_index(void *d);

static void get_data_index_async(struct zsvsheet_ui_buffer *uibuffp, const char *filename, struct zsv_opts *optsp,
                                 const char *row_filter, struct zsv_prop_handler *custom_prop_handler,
                                 // const char *opts_used,
                                 pthread_mutex_t *mutexp) {
  struct zsvsheet_index_opts *ixopts = calloc(1, sizeof(*ixopts));
  ixopts->mutexp = mutexp;
  ixopts->filename = filename;
  //  ixopts->data_filenamep = &uibuffp->data_filename;
  ixopts->zsv_opts = *optsp;
  ixopts->row_filter = row_filter;
  // ixopts->index = &uibuffp->index;
  //  ixopts->index_ready = &uibuffp->index_ready;
  ixopts->custom_prop_handler = custom_prop_handler;
  //  ixopts->opts_used = opts_used;
  ixopts->uib = uibuffp;
  ixopts->uib->ixopts = ixopts;
  pthread_t thread;
  pthread_create(&thread, NULL, get_data_index, ixopts);
  pthread_detach(thread);
}

static int read_data(struct zsvsheet_ui_buffer **uibufferp,   // a new zsvsheet_ui_buffer will be allocated
                     struct zsvsheet_ui_buffer_opts *uibopts, // if *uibufferp == NULL and uibopts != NULL
                     size_t start_row, size_t start_col, size_t header_span, struct zsvsheet_opts *zsvsheet_opts,
                     struct zsv_prop_handler *custom_prop_handler) {
  const char *filename = (uibufferp && *uibufferp) ? (*uibufferp)->filename : uibopts ? uibopts->filename : NULL;
  struct zsv_opts opts = {0};
  const char *opts_used = "";
  if (uibufferp && *uibufferp)
    opts = (*uibufferp)->zsv_opts;
  else if (uibopts) {
    opts = uibopts->zsv_opts;
    opts_used = uibopts->opts_used;
  }
  struct zsvsheet_ui_buffer *uibuff = uibufferp ? *uibufferp : NULL;
  size_t remaining_rows_to_skip = start_row;
  size_t remaining_header_to_skip = header_span;
  size_t original_row_num = 0;
  const char *row_filter = uibuff ? uibuff->row_filter : NULL;
  size_t row_filter_len = row_filter ? strlen(row_filter) : 0;
  FILE *fp;

  assert(filename != NULL);
  fp = fopen(filename, "rb");
  if (!fp)
    return errno;

  opts.stream = fp; // Input file stream

  zsv_parser parser = {0};
  if (zsv_new_with_properties(&opts, custom_prop_handler, filename, opts_used, &parser) != zsv_status_ok) {
    fclose(fp);
    zsv_delete(parser);
    return errno ? errno : -1;
  }
  // opts_used is no longer needed since opts will be updated

  if (uibuff) {
    pthread_mutex_lock(&uibuff->mutex);
    if (uibuff->index_ready && row_filter) {
      fclose(fp);
      fp = fopen(uibuff->data_filename, "rb");
      if (!fp) {
        pthread_mutex_unlock(&uibuff->mutex);
        return errno;
      }
      opts.stream = fp;
    }

    enum zsv_index_status zst = zsv_index_status_ok;
    if (uibuff->index_ready) {
      opts.header_span = 0;
      opts.rows_to_ignore = 0;

      if (uibuff->data_filename) {
        struct zsv_opts filter_opts = {0};
        filter_opts.stream = opts.stream;
        filter_opts.max_columns = opts.max_columns;
        filter_opts.max_row_size = opts.max_row_size;
        filter_opts.max_rows = opts.max_rows;
        opts = filter_opts;
        uibuff->has_row_num = 1; // move this to coincide with when data_filename is assigned
      }
      zst = zsv_index_seek_row(uibuff->index, &opts, start_row);

      zsv_delete(parser);
      parser = zsv_new(&opts);

      remaining_header_to_skip = 0;
      remaining_rows_to_skip = 0;
      original_row_num = header_span + start_row;
    }
    pthread_mutex_unlock(&uibuff->mutex);
    if (zst != zsv_index_status_ok)
      return errno ? errno : -1;
  }

  size_t rows_read = header_span;

  size_t find_len = zsvsheet_opts->find ? strlen(zsvsheet_opts->find) : 0;
  size_t rows_searched = 0;
  zsvsheet_screen_buffer_t buffer = uibuff ? uibuff->buffer : NULL;
  if (uibuff && uibuff->has_row_num)
    zsvsheet_opts->hide_row_nums = 1;

  while (zsv_next_row(parser) == zsv_status_row &&
         (rows_read == 0 || rows_read < zsvsheet_screen_buffer_rows(buffer))) { // for each row
    if (uibuff == NULL && uibufferp && uibopts && zsv_cell_count(parser) > 0) {
      enum zsvsheet_priv_status stat;
      struct zsvsheet_ui_buffer *tmp_uibuff = NULL;
      if (!(buffer = zsvsheet_screen_buffer_new(zsv_cell_count(parser), uibopts->buff_opts, &stat)) ||
          stat != zsvsheet_priv_status_ok || !(tmp_uibuff = zsvsheet_ui_buffer_new(buffer, uibopts))) {
        if (tmp_uibuff)
          zsvsheet_ui_buffer_delete(tmp_uibuff);
        else
          zsvsheet_screen_buffer_delete(buffer);
        return -1;
      }
      *uibufferp = uibuff = tmp_uibuff;
      row_filter = uibuff ? uibuff->row_filter : NULL;
      row_filter_len = row_filter ? strlen(row_filter) : 0;
    }

    // row number
    size_t rownum_column_offset = 0;
    if (rows_read == 0 && zsvsheet_opts->hide_row_nums == 0) {
      // Check if we already have Row #
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
    size_t col_count = zsv_cell_count(parser);
    if (uibuff) {
      if (col_count > uibuff->dimensions.col_count)
        uibuff->dimensions.col_count = col_count;
    }
    if (rows_read > 0 && row_filter) {
      if (!zsvsheet_found_in_row(parser, col_count, row_filter, row_filter_len))
        continue;
    }

    if (remaining_rows_to_skip > 0) {
      remaining_rows_to_skip--;
      continue;
    }

    if (zsvsheet_opts->find) { // find the next occurrence
      rows_searched++;
      if (zsvsheet_found_in_row(parser, col_count, zsvsheet_opts->find, find_len)) {
        zsvsheet_opts->found_rownum = rows_searched + start_row;
        break;
      }
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
  fclose(fp);
  zsv_delete(parser);

  char *ui_status = NULL;
  if (uibuff) {
    if (!uibuff->index_started) {
      uibuff->buff_used_rows = rows_read;
      uibuff->index_started = 1;
      if (original_row_num > 1 && (row_filter == NULL || rows_read > 0)) {
        opts.stream = NULL;
        get_data_index_async(uibuff, filename, &opts, row_filter, custom_prop_handler, /* opts_used, */ &uibuff->mutex);
        asprintf(&ui_status, "(building index) ");
      }
    }
  }

  if (ui_status) {
    pthread_mutex_lock(&uibuff->mutex);
    if (uibuff->status)
      free(uibuff->status);
    uibuff->status = ui_status;
    pthread_mutex_unlock(&uibuff->mutex);
  }

  return 0;
}

// get_data_index(): return an index for constant-time access
static void *get_data_index(void *gdi) {
  struct zsvsheet_index_opts *d = gdi;
  pthread_mutex_t *mutexp = d->mutexp;
  int *errp = d->errp;

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
  d->uib->index_ready = 1;
  // *d->index_ready = 1;

  if (d->uib) {
    free(d->uib->status);
    d->uib->status = NULL;
    if (d->row_filter != NULL) {
      if (d->uib->index->row_count > 0) {
        d->uib->dimensions.row_count = d->uib->index->row_count + 1;
        asprintf(&d->uib->status, "(%" PRIu64 " filtered rows) ", d->uib->index->row_count);
      }
    }
  }
  free(d);
  pthread_mutex_unlock(mutexp);

  return NULL;
}

static size_t zsvsheet_find_next(struct zsvsheet_ui_buffer *uib, const char *needle,
                                 struct zsvsheet_opts *zsvsheet_opts, size_t header_span,
                                 struct zsv_prop_handler *custom_prop_handler) {
  struct zsvsheet_rowcol *input_offset = &uib->input_offset;
  struct zsvsheet_rowcol *buff_offset = &uib->buff_offset;
  size_t cursor_row = uib->cursor_row;
  zsvsheet_opts->find = needle;
  zsvsheet_opts->found_rownum = 0;
  // TO DO: check if it exists in current row, later column (and change 'cursor_row - 1' below to 'cursor_row')
  read_data(&uib, NULL, input_offset->row + buff_offset->row + header_span + cursor_row - 1, 0, header_span,
            zsvsheet_opts, custom_prop_handler);
  zsvsheet_opts->find = NULL;
  return zsvsheet_opts->found_rownum;
}
