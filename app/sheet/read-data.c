#include <errno.h>
#include <zsv/utils/prop.h>
#include "sheet_internal.h"
#include "buffer.h"

#if defined(WIN32) || defined(_WIN32)
#define NO_MEMMEM
#include <zsv/utils/memmem.h>
#endif

static char *zsvsheet_found_in_row(zsv_parser parser, size_t col_count, const char *target, size_t target_len) {
  if (col_count == 0)
    return NULL;

  struct zsv_cell first_cell = zsv_get_cell(parser, 0);
  struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);

  return memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, target, target_len);
}

struct get_data_index_data {
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_t *mutexp;
#endif
  const char *filename;
  const struct zsv_opts *optsp;
  const char *row_filter;
  size_t *row_countp;
  void **indexp;
  int *errp;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

static void *get_data_index(struct get_data_index_data *d);

static void get_data_index_async(void **ix, const char *filename, const struct zsv_opts *optsp, const char *row_filter,
                                 size_t *row_countp, struct zsv_prop_handler *custom_prop_handler, const char *opts_used
#ifdef ZSVSHEET_USE_THREADS
                                 ,
                                 pthread_mutex_t *mutexp
#endif
) {
  struct get_data_index_data *gdi = calloc(1, sizeof(*gdi));
#ifdef ZSVSHEET_USE_THREADS
  gdi->mutexp = mutexp;
#endif
  gdi->filename = filename;
  gdi->optsp = optsp;
  gdi->row_filter = row_filter;
  gdi->row_countp = row_countp;
  gdi->indexp = ix;
  gdi->custom_prop_handler = custom_prop_handler;
  gdi->opts_used = opts_used;
#ifdef ZSVSHEET_USE_THREADS
  pthread_t thread;
  pthread_create(&thread, NULL, get_data_index, gdi);
  pthread_detach(thread);
#else
  get_data_index(gdi);
#endif
}

static int read_data(struct zsvsheet_ui_buffer **uibufferp,   // a new zsvsheet_ui_buffer will be allocated
                     struct zsvsheet_ui_buffer_opts *uibopts, // if *uibufferp == NULL and uibopts != NULL
                     size_t start_row, size_t start_col, size_t header_span, void *index,
                     struct zsvsheet_opts *zsvsheet_opts, struct zsv_prop_handler *custom_prop_handler,
                     const char *opts_used) {
  (void)(index); // to do
  const char *filename = (uibufferp && *uibufferp) ? (*uibufferp)->filename : uibopts ? uibopts->filename : NULL;
  struct zsv_opts opts = {0};
  if (uibufferp && *uibufferp)
    opts = (*uibufferp)->zsv_opts;
  else if (uibopts)
    opts = uibopts->zsv_opts;

  assert(filename != NULL);
  FILE *fp = fopen(filename, "rb");
  if (!fp)
    return errno;

  opts.stream = fp; // Input file stream

  size_t rows_read = header_span;

  zsv_parser parser = {0};
  if (zsv_new_with_properties(&opts, custom_prop_handler, filename, opts_used, &parser) != zsv_status_ok) {
    fclose(fp);
    zsv_delete(parser);
    return errno ? errno : -1;
  }

  size_t original_row_num = 0;
  size_t remaining_header_to_skip = header_span;
  size_t remaining_rows_to_skip = start_row;
  size_t find_len = zsvsheet_opts->find ? strlen(zsvsheet_opts->find) : 0;
  size_t rows_searched = 0;
  struct zsvsheet_ui_buffer *uibuff = uibufferp ? *uibufferp : NULL;
  const char *row_filter = uibuff ? uibuff->row_filter : NULL;
  size_t row_filter_len = row_filter ? strlen(row_filter) : 0;
  zsvsheet_buffer_t buffer = uibuff ? uibuff->buffer : NULL;
  while (zsv_next_row(parser) == zsv_status_row &&
         (rows_read == 0 || rows_read < zsvsheet_buffer_rows(buffer))) { // for each row
    if (uibuff == NULL && uibufferp && uibopts && zsv_cell_count(parser) > 0) {
      enum zsvsheet_status stat;
      struct zsvsheet_ui_buffer *tmp_uibuff = NULL;
      if (!(buffer = zsvsheet_buffer_new(zsv_cell_count(parser), uibopts->buff_opts, &stat)) ||
          stat != zsvsheet_status_ok || !(tmp_uibuff = zsvsheet_ui_buffer_new(buffer, uibopts))) {
        zsv_delete(parser);
        if (tmp_uibuff)
          zsvsheet_ui_buffer_delete(tmp_uibuff);
        else
          zsvsheet_buffer_delete(buffer);
        return -1;
      }
      *uibufferp = uibuff = tmp_uibuff;
      row_filter = uibuff ? uibuff->row_filter : NULL;
      row_filter_len = row_filter ? strlen(row_filter) : 0;
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

    // row number
    size_t rownum_column_offset = 0;
    if (zsvsheet_opts->hide_row_nums == 0) {
      if (rows_read == 0) // header
        zsvsheet_buffer_write_cell(buffer, 0, 0, (const unsigned char *)"Row #");
      /////
      else {
        char buff[32];
        int n = snprintf(buff, sizeof(buff), "%zu", original_row_num - 1);
        if (!(n > 0 && n < (int)sizeof(buff)))
          sprintf(buff, "########");
        zsvsheet_buffer_write_cell(buffer, rows_read, 0, (unsigned char *)buff);
      }
      rownum_column_offset = 1;
    }

    for (size_t i = start_col; i < col_count && i + rownum_column_offset < zsvsheet_buffer_cols(buffer); i++) {
      struct zsv_cell c = zsv_get_cell(parser, i);
      if (c.len)
        zsvsheet_buffer_write_cell_w_len(buffer, rows_read, i + rownum_column_offset, c.str, c.len);
    }
    rows_read++;
  }
  fclose(fp);
  zsv_delete(parser);
  if (uibuff && !uibuff->indexed) {
    uibuff->buff_used_rows = rows_read;
    uibuff->indexed = 1;
    if (original_row_num > 1 && (row_filter == NULL || rows_read > 0)) {
      opts.stream = NULL;
      get_data_index_async(&uibuff->dimensions.index, filename, &opts, row_filter, &uibuff->dimensions.row_count,
                           custom_prop_handler, opts_used
#ifdef ZSVSHEET_USE_THREADS
                           ,
                           &uibuff->mutex
#endif
      );
    }
    if (row_filter != NULL) {
#ifdef ZSVSHEET_USE_THREADS
      pthread_mutex_lock(uibuff->mutex);
#endif
      free(uibuff->status);
      if (uibuff->dimensions.row_count > 0)
        asprintf(&uibuff->status, "(%zu filtered rows) ", uibuff->dimensions.row_count - 1);
      else
        uibuff->status = NULL;
#ifdef ZSVSHEET_USE_THREADS
      pthread_mutex_unlock(uibuff->mutex);
#endif
    }
  }
  return 0;
}

// get_data_index(): to do: return an index for constant-time access
static void *get_data_index(struct get_data_index_data *d) {
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_t *mutexp = d->mutexp;
#endif
  const char *filename = d->filename;
  const struct zsv_opts *optsp = d->optsp;
  const char *row_filter = d->row_filter;
  size_t *row_countp = d->row_countp;
  int *errp = d->errp;
  struct zsv_prop_handler *custom_prop_handler = d->custom_prop_handler;
  const char *opts_used = d->opts_used;
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
#ifdef ZSVSHEET_USE_THREADS
    pthread_mutex_lock(mutexp);
#endif
    perror(filename);
    *errp = errno;
    free(d);
#ifdef ZSVSHEET_USE_THREADS
    pthread_mutex_unlock(mutexp);
#endif
    return NULL;
  }
  struct zsv_opts opts = *optsp;
  opts.stream = fp;

  // Generate the index data
  // for now, we aren't doing anything except counting the total number of rows
  // TO DO: create an actual index
  zsv_parser parser = {0};
  if (zsv_new_with_properties(&opts, custom_prop_handler, filename, opts_used, &parser) != zsv_status_ok || !parser) {
#ifdef ZSVSHEET_USE_THREADS
    pthread_mutex_lock(mutexp);
#endif
    *errp = errno;
    fclose(fp);
    perror(NULL);
    free(d);
#ifdef ZSVSHEET_USE_THREADS
    pthread_mutex_unlock(mutexp);
#endif
    zsv_delete(parser);
    return NULL;
  }

  size_t row_count = 0;
  while (zsv_next_row(parser) == zsv_status_row) {
    if (row_count > 0 && row_filter) {
      size_t col_count = zsv_cell_count(parser);
      if (col_count == 0)
        continue;
      struct zsv_cell first_cell = zsv_get_cell(parser, 0);
      struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);
      if (!(memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, row_filter, strlen(row_filter))))
        continue;
    }
    row_count++;
  }
  fclose(fp);
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_lock(mutexp);
#endif
  *row_countp = row_count;
  // TO DO: *d->indexp = xxx;
  free(d);
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_unlock(mutexp);
#endif
  zsv_delete(parser);
  return NULL;
}

static size_t zsvsheet_find_next(struct zsvsheet_ui_buffer *uib, const char *needle,
                                 struct zsvsheet_opts *zsvsheet_opts, size_t header_span,
                                 struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  struct zsvsheet_rowcol *input_offset = &uib->input_offset;
  struct zsvsheet_rowcol *buff_offset = &uib->buff_offset;
  size_t cursor_row = uib->cursor_row;
  zsvsheet_opts->find = needle;
  zsvsheet_opts->found_rownum = 0;
  // TO DO: check if it exists in current row, later column (and change 'cursor_row - 1' below to 'cursor_row')
  read_data(&uib, NULL, input_offset->row + buff_offset->row + header_span + cursor_row - 1, 0, header_span, NULL,
            zsvsheet_opts, custom_prop_handler, opts_used);
  zsvsheet_opts->find = NULL;
  return zsvsheet_opts->found_rownum;
}
