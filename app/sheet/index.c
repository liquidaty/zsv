#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>

#include "index.h"
#include "zsv/utils/file.h"
#include "zsv/utils/writer.h"

static struct zsvsheet_index *add_line_end(struct zsvsheet_index *ix, uint64_t end) {
  size_t len = ix->line_end_len, cap = ix->line_end_capacity;

  if (len >= cap) {
    cap *= 2;
    ix = realloc(ix, sizeof(*ix) + cap * sizeof(ix->line_ends[0]));
    if (!ix)
      return NULL;

    ix->line_end_capacity = cap;
  }

  ix->line_ends[len] = end;
  ix->line_end_len++;

  return ix;
}

static void build_memory_index_row_handler(void *ctx) {
  struct zsvsheet_indexer *ixr = ctx;
  struct zsvsheet_index *ix = ixr->ix;
  uint64_t line_end = zsv_cum_scanned_length(ixr->parser) + 1;
  size_t col_count = zsv_cell_count(ixr->parser);

  if (ixr->filter) {
    if (col_count == 0)
      return;

    if (ixr->ix->header_line_end) {
      struct zsv_cell first_cell = zsv_get_cell(ixr->parser, 0);
      struct zsv_cell last_cell = zsv_get_cell(ixr->parser, col_count - 1);

      if (!memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, ixr->filter, ixr->filter_len))
        return;
    }

    for (size_t i = 0; i < col_count; i++) {
      struct zsv_cell cell = zsv_get_cell(ixr->parser, i);
      zsv_writer_cell(ixr->writer, i == 0, cell.str, cell.len, cell.quoted);
    }
  }

  if (!ixr->ix->header_line_end) {
    ix->header_line_end = line_end;
  } else if ((ix->row_count & (LINE_END_N - 1)) == 0) {
    if (ixr->filter) {
      if (zsv_writer_flush(ixr->writer) != zsv_writer_status_ok) {
        zsv_abort(ixr->parser);
        return;
      }
      line_end = ftell(ixr->filter_stream);
    }

    ix = add_line_end(ix, line_end);
    if (!ix) {
      zsv_abort(ixr->parser);
      return;
    }

    ixr->ix = ix;
  }

  ix->row_count++;
}

enum zsvsheet_index_status build_memory_index(struct zsvsheet_index_opts *optsp) {
  struct zsvsheet_indexer ixr = {
    .filter = optsp->row_filter,
    .filter_len = optsp->row_filter ? strlen(optsp->row_filter) : 0,
  };
  enum zsvsheet_index_status ret = zsvsheet_index_status_error;
  struct zsv_opts *zopts = optsp->zsv_optsp;
  struct zsv_opts ix_zopts = {0};
  char *temp_filename;
  FILE *temp_f = NULL;

  memcpy(&ix_zopts, zopts, sizeof(ix_zopts));

  FILE *fp = fopen(optsp->filename, "rb");
  if (!fp)
    return ret;

  ix_zopts.ctx = &ixr;
  ix_zopts.stream = fp;
  ix_zopts.row_handler = build_memory_index_row_handler;

  enum zsv_status zst =
    zsv_new_with_properties(&ix_zopts, optsp->custom_prop_handler, optsp->filename, optsp->opts_used, &ixr.parser);
  if (zst != zsv_status_ok)
    goto out;

  if (optsp->row_filter) {
    zsv_csv_writer temp_file_writer = NULL;
    unsigned char temp_buff[8196];

    temp_filename = zsv_get_temp_filename("zsvsheet_filter_XXXXXXXX");
    if (!temp_filename)
      return ret;

    *optsp->temp_filename = temp_filename;

    struct zsv_csv_writer_options writer_opts = {0};
    if (!(writer_opts.stream = temp_f = fopen(temp_filename, "wb")))
      return ret;
    if (!(temp_file_writer = zsv_writer_new(&writer_opts)))
      goto out;

    zsv_writer_set_temp_buff(temp_file_writer, temp_buff, sizeof(temp_buff));
    ixr.writer = temp_file_writer;
    ixr.filter_stream = temp_f;
  }

  const size_t initial_cap = 256;
  ixr.ix = malloc(sizeof(*ixr.ix) + initial_cap * sizeof(size_t));
  if (!ixr.ix)
    goto out;
  memset(ixr.ix, 0, sizeof(*ixr.ix));
  ixr.ix->line_end_capacity = initial_cap;

  while ((zst = zsv_parse_more(ixr.parser)) == zsv_status_ok)
    ;

  zsv_finish(ixr.parser);

  if (zst == zsv_status_no_more_input) {
    ret = zsvsheet_index_status_ok;
    *optsp->index = ixr.ix;
  } else
    free(ixr.ix);

out:
  zsv_delete(ixr.parser);
  fclose(fp);
  if (temp_f)
    fclose(temp_f);

  return ret;
}

void get_memory_index(struct zsvsheet_index *ix, uint64_t row, off_t *offset_out, size_t *remaining_rows_out) {
  if (!row || row - 1 < LINE_END_N) {
    *offset_out = (off_t)ix->header_line_end;
    *remaining_rows_out = row;
    return;
  }

  const size_t i = (row - LINE_END_N) >> LINE_END_SHIFT;
  *offset_out = (off_t)ix->line_ends[i];
  *remaining_rows_out = row & (LINE_END_N - 1);
}
