#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/index.h>
#include <zsv/utils/file.h>
#include <zsv/utils/writer.h>

#include "index.h"

static void save_filtered_file_row_handler(void *ctx) {
  struct zsvsheet_indexer *ixr = ctx;
  zsv_parser parser = ixr->parser;
  size_t col_count = zsv_cell_count(parser);
  ixr->row_num++;
  if (col_count == 0)
    return;

  if (ixr->seen_header) {
    struct zsv_cell first_cell = zsv_get_cell(parser, 0);
    struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);

    if (!memmem(first_cell.str, last_cell.str - first_cell.str + last_cell.len, ixr->filter, ixr->filter_len))
      return;
  } else {
    ixr->seen_header = 1;
  }

  char row_started = 0;
  // if(ixr->parent_rownum_display == zsvsheet_rownum_display_calculated) {
  if (ixr->parent_uib && ixr->parent_uib->rownum_display == zsvsheet_rownum_display_calculated) {
    // parent rownum was calculated; we need to create our own rownum column
    // if(!ixr->parent_uib || !ixr->parent_uib->data_filename) { // parent_uib->rownum_display ==
    // zsvsheet_rownum_display_calculated) {
    //  ixr->parent_uib && ixr->parent_uib->data_filename)
    row_started = 1;
    if (ixr->row_num == 1)
      zsv_writer_cell_s(ixr->writer, 1, (const unsigned char *)"Row #", 0); // to do: consolidate "Row #"
    else
      zsv_writer_cell_zu(ixr->writer, 1, ixr->row_num - 1);
  }
  for (size_t i = 0; i < col_count; i++) {
    struct zsv_cell cell = zsv_get_cell(parser, i);
    zsv_writer_cell(ixr->writer, i == 0 && row_started == 0, cell.str, cell.len, cell.quoted);
  }
}

static void build_memory_index_row_handler(void *ctx) {
  struct zsvsheet_indexer *ixr = ctx;
  struct zsv_index *ix = ixr->ix;
  zsv_parser parser = ixr->parser;

  if (zsv_index_add_row(ix, parser) != zsv_index_status_ok)
    zsv_abort(parser);
}

enum zsv_index_status build_memory_index(struct zsvsheet_index_opts *opts) {
  struct zsvsheet_indexer ixr = {0};
  ixr.filter = opts->row_filter;
  ixr.filter_len = opts->row_filter ? strlen(opts->row_filter) : 0;

  enum zsv_index_status ret = zsv_index_status_error;
  struct zsv_opts ix_zopts = opts->zsv_opts;
  unsigned char temp_buff[8196];
  char *temp_filename;
  FILE *temp_f = NULL;
  zsv_csv_writer temp_file_writer = NULL;
  FILE *fp = fopen(opts->filename, "rb");
  if (!fp)
    return ret;

  ix_zopts.ctx = &ixr;
  ix_zopts.stream = fp;

  ixr.parent_uib = opts->parent_uib;
  //  ixr.parent_rownum_display = opts->parent_rownum_display;
  if (opts->row_filter) {
    // temp_filename = zsv_get_temp_filename("zsvsheet_filter_XXXXXXXX");
    asprintf(&temp_filename, "/tmp/zsvsheet-tmp-%s.tmp", opts->row_filter);
    if (!temp_filename)
      return ret;

    opts->uib->data_filename = temp_filename;
    opts->uib->rownum_display = zsvsheet_rownum_display_in_data;

    struct zsv_csv_writer_options writer_opts = {0};
    if (!(writer_opts.stream = temp_f = fopen(temp_filename, "w+")))
      return ret;
    if (!(temp_file_writer = zsv_writer_new(&writer_opts)))
      goto out;

    zsv_writer_set_temp_buff(temp_file_writer, temp_buff, sizeof(temp_buff));
    ixr.writer = temp_file_writer;
    ixr.filter_stream = temp_f;
    ix_zopts.row_handler = save_filtered_file_row_handler;

    enum zsv_status zst =
      zsv_new_with_properties(&ix_zopts, opts->custom_prop_handler, opts->filename, opts->opts_used, &ixr.parser);
    if (zst != zsv_status_ok)
      goto out;

    while ((zst = zsv_parse_more(ixr.parser)) == zsv_status_ok)
      ;

    if (zst != zsv_status_no_more_input)
      goto out;

    zsv_finish(ixr.parser);
    zsv_delete(ixr.parser);
    zsv_writer_delete(temp_file_writer);
    temp_file_writer = NULL;
    if (fseek(temp_f, 0, SEEK_SET))
      goto out;

    ix_zopts.stream = temp_f;
  }

  ix_zopts.row_handler = build_memory_index_row_handler;

  enum zsv_status zst =
    zsv_new_with_properties(&ix_zopts, opts->custom_prop_handler, opts->filename, opts->opts_used, &ixr.parser);
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
    opts->uib->index = ixr.ix;
    //    *opts->index =
  } else
    free(ixr.ix);

out:
  zsv_delete(ixr.parser);
  fclose(fp);
  if (temp_file_writer)
    zsv_writer_delete(temp_file_writer);
  if (temp_f)
    fclose(temp_f);

  return ret;
}
