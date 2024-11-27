#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <zsv/utils/mem.h>
#include <zsv/ext/sheet.h>
#include "transformation.h"
#include "handlers_internal.h"

struct filtered_file_ctx {
  char *filter;
  size_t filter_len;
  size_t row_num; // 1-based row number (1 = header row, 2 = first data row)
  size_t passed;
  unsigned char seen_header : 1;
  unsigned char has_row_num : 1;
  unsigned char _ : 7;
};

static void zsvsheet_save_filtered_file_row_handler(zsvsheet_transformation trn) {
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

  ctx->passed++;
}

static void zsvsheet_filter_file_on_done(zsvsheet_transformation trn) {
  struct filtered_file_ctx *ctx = zsvsheet_transformation_user_context(trn);
  struct zsvsheet_ui_buffer *uib = trn->ui_buffer;

  char *status = NULL;
  asprintf(&status, "(%zu filtered rows) ", ctx->passed - 1);

  pthread_mutex_lock(&uib->mutex);
  char *old_status = uib->status;
  uib->status = status;
  pthread_mutex_unlock(&uib->mutex);

  free(old_status);
  free(ctx->filter);
}

static enum zsvsheet_status zsvsheet_filter_file(zsvsheet_proc_context_t proc_ctx, const char *row_filter) {
  struct filtered_file_ctx ctx = {
    .seen_header = 0,
    .row_num = 0,
    .passed = 0,
    .has_row_num = 0,
    .filter = strdup(row_filter),
    .filter_len = strlen(row_filter),
  };
  struct zsvsheet_buffer_transformation_opts opts = {
    .user_context = zsv_memdup(&ctx, sizeof(ctx)),
    .row_handler = zsvsheet_save_filtered_file_row_handler,
    .on_done = zsvsheet_filter_file_on_done,
  };

  return zsvsheet_push_transformation(proc_ctx, opts);
}
