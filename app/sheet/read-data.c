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

// Parser column where a row's data begins: a data file that carries its own leading "Row #"
// column (filter, sort or pivot output) has no data there
static size_t zsvsheet_find_base_col(const struct zsvsheet_ui_buffer *uib) {
  return uib && uib->has_row_num ? 1 : 0;
}

// Screen columns that precede parser column 0: sheet's own row-number column, which a data
// file that carries its own "Row #" already counts
static size_t zsvsheet_find_col_offset(const struct zsvsheet_ui_buffer *uib) {
  return uib->rownum_col_offset && !uib->has_row_num ? 1 : 0;
}

// zsvsheet_found_in_row: return 0 if not found, else 1-based index of column. Indexes here
// are parser columns, which this pass writes at screen column `col_offset` + index; a cell
// whose screen column is hidden is skipped, so a find cannot land out of sight.
// col_stop bounds the scan (exclusive; SIZE_MAX: through the last column); keep_last returns
// the last match in the range rather than the first
static size_t zsvsheet_found_in_row(struct zsvsheet_ui_buffer *uib, zsv_parser parser, size_t col_offset,
                                    size_t col_start, size_t col_count, size_t col_stop,
                                    const struct zsvsheet_pattern *pattern, size_t specified_column_plus_1,
                                    char keep_last) {
  size_t col_end = col_count;
  size_t found = 0;

  if (col_stop < col_end)
    col_end = col_stop;
  if (col_start >= col_end)
    return 0;

  // cheap reject before matching cell by cell (a no-op for a regex)
  struct zsv_cell first_cell = zsv_get_cell(parser, col_start);
  struct zsv_cell last_cell = zsv_get_cell(parser, col_end - 1);
  if (!zsvsheet_pattern_span_might_match(pattern, first_cell.str,
                                         (size_t)(last_cell.str - first_cell.str) + last_cell.len))
    return 0;

  for (size_t i = col_start; i < col_end; i++) {
    if ((specified_column_plus_1 == 0 || i + 1 == specified_column_plus_1) &&
        !zsvsheet_ui_buffer_col_hidden(uib, i + col_offset)) {
      struct zsv_cell c = zsv_get_cell(parser, i);
      if (zsvsheet_pattern_match(pattern, c.str, c.len)) {
        found = i + 1;
        if (!keep_last)
          break;
      }
    }
  }
  return found;
}

static void *get_data_index(void *d);

// Whether a file parsed with these (effective) options reads back unchanged from the
// comma-delimited CSV that saving writes: no other delimiter, and nothing that skips,
// adds, merges or rewrites rows or values as they are read
static char zsvsheet_opts_plain_csv(const struct zsv_opts *o) {
  return (!o->delimiter || o->delimiter == ',') && o->header_span <= 1 && !o->rows_to_ignore && !o->no_quotes &&
         !o->insert_header_row && !o->max_rows && !o->keep_empty_header_rows && !o->malformed_utf8_replace &&
         !o->overwrite_auto && !o->overwrite.open
#ifndef ZSV_NO_ONLY_CRLF
         && !o->only_crlf_rowend // saving writes LF row ends
#endif
    ;
}

// Show pending edits in data rows just loaded into the screen buffer, where buffer row
// b >= 1 is raw row start_row + b. Buffer row 0, the header, is loaded only when the file
// is opened and a reload leaves it (and any edit written to it) in place
static enum zsvsheet_priv_status zsvsheet_ui_buffer_apply_edits(struct zsvsheet_ui_buffer *uib, size_t start_row,
                                                                size_t rows_loaded) {
  const struct zsvsheet_edits *e = &uib->edits;
  const size_t col_offset = zsvsheet_ui_buffer_data_col_offset(uib);
  const size_t cols = zsvsheet_screen_buffer_cols(uib->buffer);
  enum zsvsheet_priv_status stat = zsvsheet_priv_status_ok;
  for (size_t i = zsvsheet_edits_lower_bound(e, start_row + 1, 0);
       i < e->count && e->items[i].row < start_row + rows_loaded && stat == zsvsheet_priv_status_ok; i++) {
    const struct zsvsheet_cell_edit *c = &e->items[i];
    if (c->col + col_offset < cols)
      stat =
        zsvsheet_screen_buffer_write_cell_w_len(uib->buffer, c->row - start_row, c->col + col_offset, c->value, c->len);
  }
  return stat;
}

static void get_data_index_async(struct zsvsheet_ui_buffer *uibuffp, const char *filename, struct zsv_opts *optsp,
                                 struct zsv_prop_handler *custom_prop_handler, char *old_ui_status) {
  struct zsvsheet_index_opts *ixopts = calloc(1, sizeof(*ixopts));
  if (!ixopts) { // the index is an optimization; without it the buffer still works
    zsvsheet_ui_buffer_index_done(uibuffp, old_ui_status);
    return;
  }
  ixopts->mutexp = &uibuffp->mutex;
  ixopts->filename = filename;
  ixopts->zsv_opts = *optsp;
  ixopts->custom_prop_handler = custom_prop_handler;
  ixopts->uib = uibuffp;
  ixopts->uib->ixopts = ixopts;
  ixopts->old_ui_status = old_ui_status;

  if (uibuffp->worker_active)
    zsvsheet_ui_buffer_join_worker(uibuffp);
  if (zsvsheet_ui_buffer_create_worker(uibuffp, get_data_index, ixopts) != 0) {
    zsvsheet_ui_buffer_index_done(uibuffp, old_ui_status);
    uibuffp->ixopts = NULL;
    free(ixopts);
  }
}

static int read_data(struct zsvsheet_ui_buffer **uibufferp,   // a new zsvsheet_ui_buffer will be allocated
                     struct zsvsheet_ui_buffer_opts *uibopts, // if *uibufferp == NULL and uibopts != NULL
                     size_t start_row, size_t start_col, size_t header_span, struct zsvsheet_opts *zsvsheet_opts,
                     struct zsv_prop_handler *custom_prop_handler) {
  int rc = 0;
  struct uib_parse_errs parse_errs = {0};
  uib_parse_errs_init(&parse_errs, 100);
  zsv_parser parser = {0};
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
      // We resume mid-file, not at a header, so a blank row here is data, not an
      // empty header row. Without this the parser silently drops leading blank
      // rows and every row number reported from this pass is short by the number
      // it dropped -- see the same fix for mid-file workers in app/count.c
      opts.keep_empty_header_rows = 1;

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

  size_t rows_searched = 0;
  size_t find_col = zsvsheet_opts->find ? zsvsheet_opts->find_start_colnum : 0;
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
        uibuff->custom_prop_handler = custom_prop_handler;
        uibuff->filename_plain_csv = zsvsheet_opts_plain_csv(&opts);
        uibuff->parse_errs = parse_errs;            // transfer errors
        memset(&parse_errs, 0, sizeof(parse_errs)); // prevent double-free
        // the index worker can outlive caller-owned uibopts strings, so from
        // here on point filename at the copy owned by (and freed with) uibuff
        if (uibuff->data_filename)
          filename = uibuff->data_filename;
        else if (uibuff->filename)
          filename = uibuff->filename;
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
        if (buffer && !buffer->opts.no_rownum_column) {
          buffer->cols = col_count;
          buffer->opts.no_rownum_column = 1;
        }
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
      const size_t rownum = ++rows_searched + start_row;
      if (zsvsheet_opts->find_stop_rownum && rownum > zsvsheet_opts->find_stop_rownum)
        break; // past the range: the last match, if any, is already recorded
      // the screen column this pass writes parser column 0 at, as the cell writes below do
      const size_t find_col_offset = zsvsheet_opts->hide_row_nums ? 0 : 1;
      const size_t col_stop = zsvsheet_opts->find_stop_rownum && rownum == zsvsheet_opts->find_stop_rownum
                                ? zsvsheet_opts->find_stop_colnum
                                : (size_t)-1;
      size_t colIndexPlus1 =
        zsvsheet_found_in_row(uibuff, parser, find_col_offset, find_col, col_count, col_stop, zsvsheet_opts->find,
                              zsvsheet_opts->find_specified_column_plus_1, zsvsheet_opts->find_last);
      if (colIndexPlus1) {
        zsvsheet_opts->found_rownum = rownum;
        zsvsheet_opts->found_colnum = colIndexPlus1 - 1;
        if (!zsvsheet_opts->find_last)
          break;
      }
      // next row: search from its first data cell, past a "Row #" the file carries
      find_col = zsvsheet_find_base_col(uibuff);
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

    // Write every cell across the buffer's width, including empty and missing ones:
    // this buffer row may still hold a longer row from a previous load, and an
    // unwritten cell would keep showing that stale content
    for (size_t i = start_col; i + rownum_column_offset < zsvsheet_screen_buffer_cols(buffer); i++) {
      struct zsv_cell c = i < col_count ? zsv_get_cell(parser, i) : (struct zsv_cell){0};
      zsvsheet_screen_buffer_write_cell_w_len(buffer, rows_read, i + rownum_column_offset, c.str, c.len);
    }

    rows_read++;
  }
  if (uibuff && !zsvsheet_opts->find && zsvsheet_ui_buffer_apply_edits(uibuff, start_row, rows_read)) {
    rc = -1;
    goto done;
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
    uibuff->buff_used_rows = rows_read;
    uibuff->dimensions.row_count = rows_read;

    if (original_row_num > 1 && rows_read > 0) {
      char *ix_placeholder; // asprintf leaves its output indeterminate on failure,
      if (asprintf(&ix_placeholder, "%s(building index) ", old_ui_status ? old_ui_status : "") == -1) {
        rc = -1; // so on failure leave uibuff->status holding old_ui_status
        goto done;
      }
      zsvsheet_ui_buffer_take_index_placeholder(uibuff, ix_placeholder);

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
  struct zsvsheet_ui_buffer *uib = d->uib;

  enum zsv_index_status ix_status = build_memory_index(d);

  pthread_mutex_lock(mutexp);
  if (ix_status != zsv_index_status_ok && d->errp != NULL)
    *d->errp = errno;
  uib->ixopts = NULL; // ui_buffer_delete writes through ixopts if left set
  pthread_mutex_unlock(mutexp);

  zsvsheet_ui_buffer_index_done(uib, d->old_ui_status); // takes ownership either way
  free(d);

  return NULL;
}

// Search the in-memory screen buffer for a match in the row-major range from
// (first_row, first_col) up to (stop_row, stop_col) exclusive. Rows are raw rows and columns
// parser columns; a buffer without a data file holds them all, and row 0 is its header.
// stop_row 0 searches through the last row, and stop_col SIZE_MAX through the last column. A
// hidden column is skipped, so a find cannot land out of sight. keep_last returns the last
// match in the range rather than the first. Returns 1 and sets found_rownum/found_colnum
// (raw row, parser column) on a match
static char zsvsheet_find_in_buffer(struct zsvsheet_ui_buffer *uib, struct zsvsheet_opts *zsvsheet_opts,
                                    size_t first_row, size_t first_col, size_t stop_row, size_t stop_col,
                                    char keep_last) {
  const struct zsvsheet_pattern *pattern = zsvsheet_opts->find;
  const zsvsheet_screen_buffer_t buffer = uib->buffer;
  const size_t row_count = uib->dimensions.row_count;
  const size_t base_col = zsvsheet_find_base_col(uib);
  // parser-to-screen column offset, as the file path computes it
  const size_t col_offset = zsvsheet_find_col_offset(uib);
  const size_t buffer_cols = zsvsheet_screen_buffer_cols(buffer);
  const size_t col_count = buffer_cols > col_offset ? buffer_cols - col_offset : 0;
  char found = 0;
  size_t found_row = 0, found_col = 0;

  if (!pattern)
    return 0;
  for (size_t row = first_row; row < row_count && (!stop_row || row <= stop_row); row++) {
    const size_t col_start = row == first_row ? first_col : base_col;
    size_t col_end = col_count;
    if (row < uib->input_offset.row)
      continue;
    if (stop_row && row == stop_row && stop_col < col_end)
      col_end = stop_col;
    for (size_t col = col_start; col < col_end; col++) {
      const unsigned char *cell;
      if (zsvsheet_opts->find_specified_column_plus_1 && col + 1 != zsvsheet_opts->find_specified_column_plus_1)
        continue;
      if (zsvsheet_ui_buffer_col_hidden(uib, col + col_offset))
        continue;
      cell = zsvsheet_screen_buffer_cell_display(buffer, row - uib->input_offset.row, col + col_offset);
      if (cell && zsvsheet_pattern_match(pattern, cell, strlen((const char *)cell))) {
        found = 1;
        found_row = row;
        found_col = col;
        if (!keep_last)
          goto done;
      }
    }
  }
done:
  zsvsheet_opts->found_rownum = found_row;
  zsvsheet_opts->found_colnum = found_col;
  return found;
}

// One row-major scan of the buffer's cells in the range from (first_row, first_col) up to
// (stop_row, stop_col) exclusive: a static buffer's screen buffer, or a data file parsed by
// read_data(), whose row index skips to the range's first row. See zsvsheet_find_in_buffer()
// for the bounds and keep_last. Returns 1 if a match was found, 0 if not, -1 on a read
// error, and sets found_rownum/found_colnum on a match
static int zsvsheet_find_range(struct zsvsheet_ui_buffer *uib, struct zsvsheet_opts *zsvsheet_opts, size_t header_span,
                               struct zsv_prop_handler *custom_prop_handler, size_t first_row, size_t first_col,
                               size_t stop_row, size_t stop_col, char keep_last) {
  // read_data() reports a data row as raw row header_span + start_row, and these ranges pass
  // start_row = first_row - 1, which is only the same numbering while the header is one row
  // (sheet always passes 1)
  assert(header_span == 1);
  zsvsheet_opts->found_rownum = 0;
  zsvsheet_opts->found_colnum = 0;
  if (first_row < header_span || (stop_row && first_row > stop_row) || (first_row == stop_row && first_col >= stop_col))
    return 0;
  if (!zsvsheet_ui_buffer_has_file(uib))
    return zsvsheet_find_in_buffer(uib, zsvsheet_opts, first_row, first_col, stop_row, stop_col, keep_last) ? 1 : 0;

  zsvsheet_opts->find_start_colnum = first_col;
  zsvsheet_opts->find_stop_rownum = stop_row;
  zsvsheet_opts->find_stop_colnum = stop_col;
  zsvsheet_opts->find_last = keep_last;
  if (read_data(&uib, NULL, first_row - 1, 0, header_span, zsvsheet_opts, custom_prop_handler))
    return -1;
  return zsvsheet_opts->found_rownum != 0;
}

// The last match before the cursor cell, whose row's scan stops at stop_col. A file with a
// row index is scanned a screen buffer's worth of rows at a time from the cursor backwards,
// so a nearby match costs a chunk rather than the whole prefix; without one, a single scan
// from the top does the same work. Returns 1 if found, 0 if not, -1 on a read error
static int zsvsheet_find_prev(struct zsvsheet_ui_buffer *uib, struct zsvsheet_opts *zsvsheet_opts, size_t header_span,
                              struct zsv_prop_handler *custom_prop_handler, size_t cursor_row, size_t stop_col) {
  const size_t base_col = zsvsheet_find_base_col(uib);
  const size_t chunk = zsvsheet_screen_buffer_rows(uib->buffer);
  size_t end = cursor_row, end_col = stop_col;

  if (!zsvsheet_ui_buffer_has_file(uib) || !zsvsheet_ui_buffer_index_ready(uib, 0))
    return zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, header_span, base_col, cursor_row,
                               stop_col, 1);
  while (end >= header_span) {
    const size_t start = end > header_span + chunk - 1 ? end - chunk + 1 : header_span;
    int found =
      zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, start, base_col, end, end_col, 1);
    if (found > 0)
      return 1;
    if (found < 0) // e.g. a seek past a partially committed index: scan from the top instead
      break;
    if (start == header_span)
      return 0;
    end = start - 1;
    end_col = (size_t)-1;
  }
  return zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, header_span, base_col, cursor_row,
                             stop_col, 1);
}

// Search for the pattern from the cursor cell in the given direction, wrapping at the end of
// the buffer or file as vim does: the primary scan skips the cursor cell, and the wrap scan
// includes it, so a sole match is found again with the wrap message and the cursor stays.
// start_col is the parser column after the cursor cell. On a match, sets found_rownum and
// found_colnum and returns 1, and *wrapped says the wrap scan found it. Returns 0 when
// nothing matches, -1 on a read error
static int zsvsheet_find_search(struct zsvsheet_ui_buffer *uib, struct zsvsheet_opts *zsvsheet_opts, size_t header_span,
                                struct zsv_prop_handler *custom_prop_handler, size_t cursor_row, size_t start_col,
                                char backward, char *wrapped) {
  const size_t base_col = zsvsheet_find_base_col(uib);
  size_t row = cursor_row, start = start_col;
  size_t cursor_col;
  int found;

  *wrapped = 0;
  // the header row is outside the search space, so a search from it starts at the data's top
  if (row < header_span) {
    row = header_span;
    start = base_col;
  }
  cursor_col = start ? start - 1 : 0;

  if (!backward) {
    found = zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, row, start, 0, (size_t)-1, 0);
    if (found == 0) { // from the top through the cursor cell: the first match is a wrapped one
      found =
        zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, header_span, base_col, row, start, 0);
      if (found > 0)
        *wrapped = 1;
    }
  } else {
    found = 0;
    if (row > header_span || cursor_col > base_col)
      found = zsvsheet_find_prev(uib, zsvsheet_opts, header_span, custom_prop_handler, row, cursor_col);
    if (found == 0) { // from the cursor cell through the last row: its last match is the previous one
      const size_t wrap_col = start > base_col ? start - 1 : base_col;
      found =
        zsvsheet_find_range(uib, zsvsheet_opts, header_span, custom_prop_handler, row, wrap_col, 0, (size_t)-1, 1);
      if (found > 0)
        *wrapped = 1;
    }
  }
  return found;
}
