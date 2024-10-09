#include <errno.h>
#include <zsv/utils/prop.h>
#include "sheet_internal.h"
#include "buffer.h"

#if defined(WIN32) || defined(_WIN32)
#define NO_MEMMEM
#include <zsv/utils/memmem.h>
#endif

static char *ztv_found_in_row(zsv_parser parser, size_t col_count, const char *target, size_t target_len) {
  if (col_count == 0)
    return NULL;

  struct zsv_cell first_cell = zsv_get_cell(parser, 0);
  struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);

  return memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, target, target_len);
}

static int read_data(
  zsv_sheet_buffer_t *bufferp,
  struct zsv_sheet_buffer_opts *buff_opts, // if buff_opts is provided, then a new *buffer will be allocated
  const char *filename, const struct zsv_opts *zsv_optsp, size_t *max_col_countp, const char *row_filter,
  size_t start_row, size_t start_col, size_t header_span, void *index, struct ztv_opts *ztv_opts,
  struct zsv_prop_handler *custom_prop_handler, const char *opts_used, size_t *rows_readp) {
  (void)(index); // to do
  FILE *fp = fopen(filename, "rb");
  if (!fp)
    return errno;

  struct zsv_opts opts = *zsv_optsp;
  opts.stream = fp; // Input file stream

  size_t rows_read = header_span;

  // Create and run the parser
  zsv_parser parser = {0};
  if (zsv_new_with_properties(&opts, custom_prop_handler, filename, opts_used, &parser) != zsv_status_ok) {
    fclose(fp);
    zsv_delete(parser);
    return errno ? errno : -1;
  }

  size_t original_row_num = 0;
  size_t remaining_header_to_skip = header_span;
  size_t remaining_rows_to_skip = start_row;
  size_t row_filter_len = row_filter ? strlen(row_filter) : 0;
  size_t find_len = ztv_opts->find ? strlen(ztv_opts->find) : 0;
  size_t rows_searched = 0;
  zsv_sheet_buffer_t buffer = bufferp ? *bufferp : NULL;
  while (zsv_next_row(parser) == zsv_status_row &&
         (rows_read == 0 || rows_read < zsv_sheet_buffer_rows(buffer))) { // for each row
    if (buffer == NULL && buff_opts && zsv_cell_count(parser) > 0) {
      enum zsv_sheet_buffer_status stat;
      buffer = zsv_sheet_buffer_new(zsv_cell_count(parser), buff_opts, &stat);
      if (buffer == NULL && stat != zsv_sheet_buffer_status_ok) {
        zsv_delete(parser);
        return -1;
      }
      *bufferp = buffer;
    }
    original_row_num++;
    if (remaining_header_to_skip > 0) {
      remaining_header_to_skip--;
      continue;
    }
    size_t col_count = zsv_cell_count(parser);
    if (max_col_countp && col_count > *max_col_countp)
      *max_col_countp = col_count;
    if (rows_read > 0 && row_filter) {
      if (!ztv_found_in_row(parser, col_count, row_filter, row_filter_len))
        continue;
    }

    if (remaining_rows_to_skip > 0) {
      remaining_rows_to_skip--;
      continue;
    }

    if (ztv_opts->find) { // find the next occurrence
      rows_searched++;
      if (ztv_found_in_row(parser, col_count, ztv_opts->find, find_len)) {
        ztv_opts->found_rownum = rows_searched + start_row;
        break;
      }
      continue;
    }

    // row number
    size_t rownum_column_offset = 0;
    if (ztv_opts->hide_row_nums == 0) { // to do: merge w zsv_sheet_buffer_opts.no_rownum_column
      if (rows_read == 0)               // header
        zsv_sheet_buffer_write_cell(buffer, 0, 0, (const unsigned char *)"Row #");
      /////
      else {
        char buff[32];
        int n = snprintf(buff, sizeof(buff), "%zu", original_row_num - 1);
        if (!(n > 0 && n < (int)sizeof(buff)))
          sprintf(buff, "########");
        zsv_sheet_buffer_write_cell(buffer, rows_read, 0, (unsigned char *)buff);
      }
      rownum_column_offset = 1;
    }

    for (size_t i = start_col; i < col_count && i + rownum_column_offset < zsv_sheet_buffer_cols(buffer); i++) {
      struct zsv_cell c = zsv_get_cell(parser, i);
      if (c.len)
        zsv_sheet_buffer_write_cell_w_len(buffer, rows_read, i + rownum_column_offset, c.str, c.len);
    }
    rows_read++;
  }
  fclose(fp);
  if (rows_readp)
    *rows_readp = rows_read;
  zsv_delete(parser);
  return 0;
}

struct get_data_index_data {
#ifdef ZTV_USE_THREADS
  pthread_mutex_t *mutexp;
#endif
  const char *filename;
  struct zsv_opts *optsp;
  const char *row_filter;
  size_t *row_countp;
  void **indexp;
  int *errp;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

// get_data_index(): to do: return an index for constant-time access
static void *get_data_index(struct get_data_index_data *d) {
#ifdef ZTV_USE_THREADS
  pthread_mutex_t *mutexp = d->mutexp;
#endif
  const char *filename = d->filename;
  struct zsv_opts *optsp = d->optsp;
  const char *row_filter = d->row_filter;
  size_t *row_countp = d->row_countp;
  int *errp = d->errp;
  struct zsv_prop_handler *custom_prop_handler = d->custom_prop_handler;
  const char *opts_used = d->opts_used;
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
#ifdef ZTV_USE_THREADS
    pthread_mutex_lock(mutexp);
#endif
    perror(filename);
    *errp = errno;
    free(d);
#ifdef ZTV_USE_THREADS
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
#ifdef ZTV_USE_THREADS
    pthread_mutex_lock(mutexp);
#endif
    *errp = errno;
    fclose(fp);
    perror(NULL);
    free(d);
#ifdef ZTV_USE_THREADS
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
#ifdef ZTV_USE_THREADS
  pthread_mutex_lock(mutexp);
#endif
  *row_countp = row_count;
  // TO DO: *d->indexp = xxx;
  free(d);
#ifdef ZTV_USE_THREADS
  pthread_mutex_unlock(mutexp);
#endif
  zsv_delete(parser);
  return NULL;
}

static void get_data_index_async(void **ix, const char *filename, struct zsv_opts *optsp, const char *row_filter,
                                 size_t *row_countp, struct zsv_prop_handler *custom_prop_handler, const char *opts_used
#ifdef ZTV_USE_THREADS
                                 ,
                                 pthread_mutex_t *mutexp
#endif
) {
  struct get_data_index_data *gdi = calloc(1, sizeof(*gdi));
#ifdef ZTV_USE_THREADS
  gdi->mutexp = mutexp;
#endif
  gdi->filename = filename;
  gdi->optsp = optsp;
  gdi->row_filter = row_filter;
  gdi->row_countp = row_countp;
  gdi->indexp = ix;
  gdi->custom_prop_handler = custom_prop_handler;
  gdi->opts_used = opts_used;
#ifdef ZTV_USE_THREADS
  pthread_t thread;
  pthread_create(&thread, NULL, get_data_index, gdi);
  pthread_detach(thread);
#else
  get_data_index(gdi);
#endif
}

static size_t ztv_find_next(const char *filename, const char *row_filter, const char *needle, struct zsv_opts *zsv_opts,
                            struct ztv_opts *ztv_opts, size_t header_span, struct ztv_rowcol *input_offset,
                            struct ztv_rowcol *buff_offset, size_t cursor_row, struct input_dimensions *input_dims,
                            struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  ztv_opts->find = needle;
  ztv_opts->found_rownum = 0;
  // TO DO: check if it exists in current row, later column (and change 'cursor_row - 1' below to 'cursor_row')
  read_data(NULL, NULL, filename, zsv_opts, &input_dims->col_count, row_filter,
            input_offset->row + buff_offset->row + header_span + cursor_row - 1, 0, header_span, NULL, ztv_opts,
            custom_prop_handler, opts_used, NULL);
  ztv_opts->find = NULL;
  return ztv_opts->found_rownum;
}
