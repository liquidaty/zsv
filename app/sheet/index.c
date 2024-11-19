#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/index.h>
#include <zsv/utils/file.h>
#include <zsv/utils/writer.h>

#include "index.h"
#include "transformation.h"

struct filtered_file_ctx {
  const char *filter;
  size_t filter_len;
  size_t row_num; // 1-based row number (1 = header row, 2 = first data row)
  unsigned char seen_header : 1;
  unsigned char has_row_num : 1;
  unsigned char _ : 7;
};

static void save_filtered_file_row_handler(void *trn) {
  struct filtered_file_ctx *ctx = zsvsheet_transformation_user_context(trn);
  zsv_parser parser = zsvsheet_transformation_parser(trn);
  zsv_csv_writer writer = zsvsheet_transformation_writer(trn);
  size_t col_count = zsv_cell_count(parser);
  ctx->row_num++;
  if (col_count == 0)
    return;

  struct zsv_cell first_cell = zsv_get_cell(parser, 0);
  if (ctx->seen_header) {
    struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);
    if (!memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, ctx->filter, ctx->filter_len))
      return;
    // future enhancement: optionally, handle if row may have unusual quotes e.g. cell1,"ce"ll2,cell3
  } else {
    ctx->seen_header = 1;
    if (first_cell.len == ZSVSHEET_ROWNUM_HEADER_LEN && !memcmp(first_cell.str, ZSVSHEET_ROWNUM_HEADER, first_cell.len))
      ctx->has_row_num = 1;
  }

  char row_started = 0;
  if (!ctx->has_row_num) {
    // create our own rownum column
    row_started = 1;
    if (ctx->row_num == 1)
      zsv_writer_cell_s(writer, 1, (const unsigned char *)"Row #", 0); // to do: consolidate "Row #"
    else
      zsv_writer_cell_zu(writer, 1, ctx->row_num - 1);
  }
  for (size_t i = 0; i < col_count; i++) {
    struct zsv_cell cell = zsv_get_cell(parser, i);
    zsv_writer_cell(writer, i == 0 && row_started == 0, cell.str, cell.len, cell.quoted);
  }
}

static enum zsv_status filter_file(struct zsvsheet_index_opts *optsp) {
  struct filtered_file_ctx ctx = {
    .seen_header = 0,
    .row_num = 0,
    .has_row_num = 0,
    .filter = optsp->row_filter,
    .filter_len = strlen(optsp->row_filter),
  };
  struct zsvsheet_transformation_opts opts = {
    .custom_prop_handler = optsp->custom_prop_handler,
    .input_filename = optsp->filename,
  };
  zsvsheet_transformation trn;
  struct zsv_opts zopts = optsp->zsv_opts;

  zopts.ctx = &ctx;
  zopts.row_handler = save_filtered_file_row_handler;
  zopts.stream = fopen(optsp->filename, "rb");

  if (!zopts.stream)
    goto out;

  opts.zsv_opts = zopts;

  enum zsv_status zst = zsvsheet_transformation_new(opts, &trn);
  if (zst != zsv_status_ok)
    return zst;

  zsv_parser parser = zsvsheet_transformation_parser(trn);

  while ((zst = zsv_parse_more(parser)) == zsv_status_ok)
    ;

  switch (zst) {
  case zsv_status_no_more_input:
  case zsv_status_cancelled:
    break;
  default:
    goto out;
  }

  zst = zsv_finish(parser);
  if (zst != zsv_status_ok)
    goto out;

  if (asprintf(optsp->data_filenamep, "%s", zsvsheet_transformation_filename(trn)) == -1)
    zst = zsv_status_memory;

out:
  zsvsheet_transformation_delete(trn);
  return zst;
}

static void build_memory_index_row_handler(void *ctx) {
  struct zsvsheet_indexer *ixr = ctx;
  struct zsv_index *ix = ixr->ix;
  zsv_parser parser = ixr->parser;

  if (zsv_index_add_row(ix, parser) != zsv_index_status_ok)
    zsv_abort(parser);
}

enum zsv_index_status build_memory_index(struct zsvsheet_index_opts *optsp) {
  struct zsvsheet_indexer ixr = {0};
  enum zsv_index_status ret = zsv_index_status_error;
  struct zsv_opts ix_zopts = optsp->zsv_opts;

  if (optsp->row_filter) {
    enum zsv_status zst = filter_file(optsp);
    if (zst != zsv_status_ok)
      goto out;

    ix_zopts.stream = fopen(*optsp->data_filenamep, "rb");
  } else {
    ix_zopts.stream = fopen(optsp->filename, "rb");
  }

  if (!ix_zopts.stream)
    goto out;

  ix_zopts.ctx = &ixr;
  ix_zopts.row_handler = build_memory_index_row_handler;

  enum zsv_status zst =
    zsv_new_with_properties(&ix_zopts, optsp->custom_prop_handler, optsp->filename, NULL, &ixr.parser);
  if (zst != zsv_status_ok)
    goto out;

  ixr.ix = zsv_index_new();
  if (!ixr.ix)
    goto out;

  while ((zst = zsv_parse_more(ixr.parser)) == zsv_status_ok)
    ;

  zsv_finish(ixr.parser);

  if (zst == zsv_status_no_more_input) {
    ret = zsv_index_status_ok;
    // *optsp->index = ixr.ix;
    optsp->uib->index = ixr.ix;
  } else
    zsv_index_delete(ixr.ix);

out:
  if (ixr.parser)
    zsv_delete(ixr.parser);
  if (ix_zopts.stream)
    fclose(ix_zopts.stream);

  return ret;
}
