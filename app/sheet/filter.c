#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <zsv/utils/mem.h>
#include <zsv/ext/sheet.h>
#include "transformation.h"
#include "handlers_internal.h"

#ifdef HAVE_PCRE2_8
#include "../utils/pcre2-8/pcre2-8.h"
#endif

struct filtered_file_ctx {
  char *filter;
  size_t filter_len;
#ifdef HAVE_PCRE2_8
  regex_handle_t *regex;
#endif
  size_t row_num; // 1-based row number (1 = header row, 2 = first data row)
  size_t passed;
  size_t single_row_ix_plus_1;
  unsigned char seen_header : 1;
  unsigned char has_row_num : 1;
  unsigned char _ : 7;
};

// zsvsheet_nullify_row_buff: return 1 if row has overwrite
static int zsvsheet_nullify_row_buff(zsv_parser z) {
  unsigned int j = zsv_cell_count(z);
  unsigned char *end = NULL;
  int have_overwrite = 0;
  for (unsigned int i = 0; i < j; i++) {
    struct zsv_cell cell = zsv_get_cell(z, i);
    if (cell.overwritten)
      have_overwrite = 1;
    if (end) {
      while (end < cell.str) {
        *end = '\0';
        end++;
      }
    }
    end = cell.str + cell.len;
  }
  return have_overwrite;
}

static void zsvsheet_save_filtered_file_row_handler(zsvsheet_transformation trn) {
  struct filtered_file_ctx *ctx = zsvsheet_transformation_user_context(trn);
  zsv_parser parser = zsvsheet_transformation_parser(trn);
  zsv_csv_writer writer = zsvsheet_transformation_writer(trn);
  size_t col_count = zsv_cell_count(parser);
  const size_t single_row_ix_plus_1 = ctx->single_row_ix_plus_1;

  ctx->row_num++;
  if (col_count == 0)
    return;
  if (ctx->seen_header) {
    size_t start_ix = 0;
    if (single_row_ix_plus_1) {
      col_count = single_row_ix_plus_1;
      start_ix = single_row_ix_plus_1 - 1;
      if (ctx->has_row_num)
        col_count++, start_ix++;
    }
    int have_overwrite = single_row_ix_plus_1 ? 0 : zsvsheet_nullify_row_buff(parser);
    if (have_overwrite || single_row_ix_plus_1) {
      // we need to do this cell by cell
      for (unsigned int i = start_ix; i < col_count; i++) {
        struct zsv_cell cell = zsv_get_cell(parser, i);
        const unsigned char *start = cell.str;
        const unsigned char *end = cell.str + cell.len;
        if (cell.len) {
#ifdef HAVE_PCRE2_8
          if (ctx->regex) {
            if (!zsv_pcre2_8_match(ctx->regex, start, end - start))
              return; // no match: don't save this row
          } else
#endif
            if (!memmem(start, end - start, ctx->filter, ctx->filter_len))
            return; // no match: don't save this row
        }
      }
    } else {
      struct zsv_cell first_cell = zsv_get_cell(parser, 0);
      struct zsv_cell last_cell = zsv_get_cell(parser, col_count - 1);
      const unsigned char *start = first_cell.str;
      const unsigned char *end = last_cell.str + last_cell.len;
      if (end > start) {
#ifdef HAVE_PCRE2_8
        if (ctx->regex) {
          if (!zsv_pcre2_8_match(ctx->regex, start, end - start))
            return; // no match: don't save this row
        } else
#endif
          if (!memmem(start, end - start, ctx->filter, ctx->filter_len))
          return; // no match: don't save this row
      }
    }
  } else {
    struct zsv_cell first_cell = zsv_get_cell(parser, 0);
    ctx->seen_header = 1;
    /// just use uibuff->has_row_num?
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

  col_count = zsv_cell_count(parser);
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
#ifdef HAVE_PCRE2_8
  zsv_pcre2_8_delete(ctx->regex);
#endif
  free(ctx->filter);
}

static enum zsvsheet_status zsvsheet_filter_file(zsvsheet_proc_context_t proc_ctx, const char *row_filter,
                                                 size_t single_row_ix_plus_1) {
  struct filtered_file_ctx ctx = {
    .seen_header = 0,
    .row_num = 0,
    .passed = 0,
    .has_row_num = 0,
    .filter = strdup(row_filter),
    .filter_len = strlen(row_filter),
#ifdef HAVE_PCRE2_8
    .regex = row_filter && *row_filter == '/' && row_filter[1] ? zsv_pcre2_8_new(row_filter + 1, 0) : NULL,
    .single_row_ix_plus_1 = single_row_ix_plus_1,
#endif
  };
  struct zsvsheet_buffer_transformation_opts opts = {
    .user_context = zsv_memdup(&ctx, sizeof(ctx)),
    .row_handler = zsvsheet_save_filtered_file_row_handler,
    .on_done = zsvsheet_filter_file_on_done,
  };

  return zsvsheet_push_transformation(proc_ctx, opts);
}
