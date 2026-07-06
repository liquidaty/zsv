#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <zsv/utils/file.h>
#include <zsv/utils/prop.h>

#include "handlers_internal.h"
#include "transformation.h"
#include "../utils/index.h"

struct zsvsheet_transformation {
  zsv_parser parser;
  FILE *input_stream; // parser input; owned once _new() succeeds, closed on delete
  zsv_csv_writer writer;
  char *output_filename;
  char *input_filename_owned; // temp input materialized from an in-memory buffer; removed on delete (else NULL)
  FILE *output_stream;
  unsigned char *output_buffer;
  int output_fileno;
  char writer_wrote;
  struct zsvsheet_transformation_opts opts;
  void *user_context;

  struct zsvsheet_ui_buffer *ui_buffer;
  char *default_status;
  void (*on_done)(zsvsheet_transformation trn);
};

static size_t transformation_write(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream) {
  struct zsvsheet_transformation *trn = stream;

  const size_t count = fwrite(ptr, size, nitems, trn->output_stream);
  trn->writer_wrote = count > 0;

  return count > 0 ? count : 0;
}

struct transformation_writer_index_ctx {
  void *index;
  zsv_csv_writer writer;
};

static void transformation_writer_index_on_row(void *p) {
  struct transformation_writer_index_ctx *ctx = p;
  uint64_t written = zsv_writer_cum_bytes_written(ctx->writer);
  zsv_index_add_row(ctx->index, written);
}

static void transformation_writer_index_delete(void *p) {
  struct transformation_writer_index_ctx *ctx = p;
  uint64_t written = zsv_writer_cum_bytes_written(ctx->writer);
  if (written)
    zsv_index_add_row(ctx->index, written);
  free(ctx);
}

enum zsv_status zsvsheet_transformation_new(struct zsvsheet_transformation_opts opts, zsvsheet_transformation *out) {
  unsigned char *temp_buff = NULL;
  char *temp_filename = NULL;
  FILE *temp_f = NULL;
  zsv_csv_writer temp_file_writer = NULL;
  struct transformation_writer_index_ctx *ctx = NULL;
  enum zsv_status zst = zsv_status_memory;

  struct zsvsheet_transformation *trn = calloc(1, sizeof(*trn));
  if (trn == NULL)
    return zst;

  trn->on_done = opts.on_done;
  trn->ui_buffer = opts.ui_buffer;
  zst = zsv_status_error;

  temp_filename = zsv_get_temp_filename("zsf");
  if (!temp_filename)
    goto free;
  trn->output_filename = temp_filename;

  if (!(temp_f = fopen(temp_filename, "wb+")))
    goto free;
  if (setvbuf(temp_f, NULL, _IONBF, 0))
    goto free;
  trn->output_stream = temp_f;

  ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    goto free;
  ctx->index = opts.index;

  struct zsv_csv_writer_options writer_opts = {
    .with_bom = 0,
    .write = transformation_write,
    .stream = trn,
    .table_init = NULL,
    .table_init_ctx = NULL,
    .on_row = transformation_writer_index_on_row,
    .on_row_ctx = ctx,
    .on_delete = transformation_writer_index_delete,
    .on_delete_ctx = ctx,
  };
  if (!(temp_file_writer = zsv_writer_new(&writer_opts)))
    goto free;

  ctx->writer = temp_file_writer;
  const size_t temp_buff_size = 2 * 1024 * 1024;
  temp_buff = malloc(temp_buff_size);
  if (!temp_buff)
    goto free;
  trn->output_buffer = temp_buff;
  zsv_writer_set_temp_buff(temp_file_writer, temp_buff, temp_buff_size);
  trn->writer = temp_file_writer;

  trn->user_context = opts.zsv_opts.ctx;
  trn->input_stream = opts.zsv_opts.stream;
  opts.zsv_opts.ctx = trn;

  zst = zsv_new_with_properties(&opts.zsv_opts, opts.custom_prop_handler, opts.input_filename, &trn->parser);
  if (zst != zsv_status_ok)
    goto free;

  *out = trn;
  return zst;

free: // reverse-acquisition order: the writer's delete-time flush writes through trn
  if (temp_file_writer)
    zsv_writer_delete(temp_file_writer); // on_delete tears down ctx
  else
    free(ctx); // writer never took ownership (and nothing was written)
  free(temp_buff);
  if (temp_f)
    fclose(temp_f);
  free(temp_filename);
  free(trn);

  return zst;
}

void zsvsheet_transformation_delete(zsvsheet_transformation trn) {
  zsv_writer_delete(trn->writer);
  zsv_delete(trn->parser);
  if (trn->input_stream)
    fclose(trn->input_stream);
  free(trn->output_filename);
  fclose(trn->output_stream);
  free(trn->output_buffer);
  if (trn->input_filename_owned) { // temp input; parser (above) already closed it
    remove(trn->input_filename_owned);
    free(trn->input_filename_owned);
  }
  free(trn->user_context); // freed here so every teardown path (worker/sync/error) releases it once
  free(trn);
}

zsv_parser zsvsheet_transformation_parser(zsvsheet_transformation trn) {
  return trn->parser;
}

zsv_csv_writer zsvsheet_transformation_writer(zsvsheet_transformation trn) {
  return trn->writer;
}

const char *zsvsheet_transformation_filename(zsvsheet_transformation trn) {
  return trn->output_filename;
}

void *zsvsheet_transformation_user_context(zsvsheet_transformation trn) {
  return trn->user_context;
}

static void *zsvsheet_run_buffer_transformation(void *arg) {
  struct zsvsheet_transformation *trn = arg;
  struct zsvsheet_ui_buffer *uib = trn->ui_buffer;
  zsv_parser parser = trn->parser;
  pthread_mutex_t *mutex = &uib->mutex;
  enum zsv_status zst;
  char *default_status = trn->default_status;

  char cancelled = 0;
  while (!cancelled && (zst = zsv_parse_more(parser)) == zsv_status_ok) {
    pthread_mutex_lock(mutex);
    cancelled = uib->worker_cancelled;
    if (trn->writer_wrote) {
      trn->writer_wrote = 0;
      zsv_index_commit_rows(uib->index);
      uib->index_ready = 1;
    }
    pthread_mutex_unlock(mutex);
  }

  if (zst == zsv_status_no_more_input || zst == zsv_status_cancelled)
    zsv_finish(parser);

  if (trn->on_done)
    trn->on_done(trn);

  zsvsheet_transformation_delete(trn); // frees user_context

  pthread_mutex_lock(mutex);
  char *buff_status_old = uib->status;
  uib->write_done = 1;
  zsv_index_commit_rows(uib->index);
  uib->index_ready = 1;
  if (buff_status_old == default_status) {
    uib->status = NULL;
    uib->status_is_index_placeholder = 0; // never set on a transformation buffer; keep the invariant local
  }
  pthread_mutex_unlock(mutex);

  if (buff_status_old == default_status)
    free(buff_status_old);

  return NULL;
}

// Write a static (in-memory) buffer's cells to a new temp CSV so a transformation
// can read it. Returns a malloc'd filename the caller owns (remove + free), or NULL.
static char *zsvsheet_buffer_to_temp_csv(struct zsvsheet_ui_buffer *uib) {
  char *tmpfn = zsv_get_temp_filename("zsvsheet_src_XXXXXXXX");
  if (!tmpfn)
    return NULL;
  zsv_csv_writer writer = zsv_writer_new(&(struct zsv_csv_writer_options){.output_path = tmpfn});
  enum zsv_writer_status wstat = writer ? zsv_writer_status_ok : zsv_writer_status_error;
  size_t rows = uib->buff_used_rows, cols = uib->dimensions.col_count;
  for (size_t r = 0; r < rows && wstat == zsv_writer_status_ok; r++)
    for (size_t c = 0; c < cols && wstat == zsv_writer_status_ok; c++) {
      const unsigned char *cell = zsvsheet_screen_buffer_cell_display(uib->buffer, r, c);
      wstat = zsv_writer_cell(writer, c == 0, cell ? cell : (const unsigned char *)"",
                              cell ? strlen((const char *)cell) : 0, 1);
    }
  zsv_writer_delete(writer); // NULL-safe
  if (wstat == zsv_writer_status_ok)
    return tmpfn;
  remove(tmpfn); // mkstemp created the file; drop it on any failure
  free(tmpfn);
  return NULL;
}

enum zsvsheet_status zsvsheet_push_transformation(zsvsheet_proc_context_t ctx,
                                                  struct zsvsheet_buffer_transformation_opts opts) {
  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  const char *filename = zsvsheet_buffer_data_filename(buff);
  char *owned_input = NULL;
  zsvsheet_transformation trn = NULL; // declared before any `goto error` so error: never reads it uninitialized
  enum zsvsheet_status stat = zsvsheet_status_error;
  struct zsvsheet_buffer_info_internal info = zsvsheet_buffer_info_internal(buff);
  struct zsv_index *index = NULL;

  // TODO: Starting a second transformation before the first ends works, but if the second is faster
  //       than the first then it can end prematurely and read a partially written row.
  //       We could override the input stream reader to wait for more data when it sees EOF
  if (info.write_in_progress && !info.write_done)
    return zsvsheet_status_busy;

  if (!(index = zsv_index_new()))
    return zsvsheet_status_memory;

  if (!filename) {
    // static/in-memory buffer (e.g. help): materialize its contents to a temp CSV to transform
    if (!(owned_input = zsvsheet_buffer_to_temp_csv(buff)))
      goto error;
    filename = owned_input;
  }

  // TODO: custom_prop_handler is not passed to extensions?
  struct zsvsheet_transformation_opts trn_opts = {
    .custom_prop_handler = NULL,
    .input_filename = filename,
    .on_done = opts.on_done,
    .ui_buffer = NULL,
    .index = index,
  };
  struct zsv_opts zopts = zsvsheet_buffer_get_zsv_opts(buff);

  zopts.ctx = opts.user_context;
  zopts.row_handler = (void (*)(void *))opts.row_handler;
  zopts.stream = fopen(filename, "rb");
  zopts.buffsize = 2 * 1024 * 1024;

  if (!zopts.stream)
    goto error;

  trn_opts.zsv_opts = zopts;

  enum zsv_status zst = zsvsheet_transformation_new(trn_opts, &trn);
  if (zst != zsv_status_ok)
    goto error;

  // Hand the temp input (if any) to the transformation so it is removed once the parser is torn down
  trn->input_filename_owned = owned_input;
  owned_input = NULL;

  // Transform part of the file to initially populate the UI buffer
  // TODO: If the transformation is a reduction that doesn't output for some time this will caus a pause
  zsv_parser parser = zsvsheet_transformation_parser(trn);
  while ((zst = zsv_parse_more(parser)) == zsv_status_ok) {
    if (trn->writer_wrote)
      break;
  }
  trn->writer_wrote = 0;

  switch (zst) {
  case zsv_status_no_more_input:
  case zsv_status_cancelled:
    if (zsv_finish(parser) != zsv_status_ok)
      goto error;
    zsv_writer_flush(trn->writer);
    break;
  case zsv_status_ok:
    break;
  default:
    goto error;
  }

  struct zsvsheet_ui_buffer_opts uibopts = {0};

  uibopts.data_filename = zsvsheet_transformation_filename(trn);
  uibopts.write_after_open = 1;

  stat = zsvsheet_open_file_opts(ctx, &uibopts);
  if (stat != zsvsheet_status_ok)
    goto error;

  struct zsvsheet_ui_buffer *nbuff = zsvsheet_buffer_current(ctx);
  trn->ui_buffer = nbuff;
  zsv_index_commit_rows(index);
  nbuff->index_started = 1;
  nbuff->index = index;

  if (zst != zsv_status_ok) {
    nbuff->write_done = 1;
    nbuff->index_ready = 1;
    if (trn->on_done)
      trn->on_done(trn);
    zsvsheet_transformation_delete(trn); // frees user_context
    zsv_index_commit_rows(index);
    return stat;
  }

  if (asprintf(&trn->default_status, "(working) Press ESC to cancel ") == -1)
    trn->default_status = NULL; // asprintf leaves its output indeterminate on failure
  nbuff->status = trn->default_status;

  if (zsvsheet_ui_buffer_create_worker(nbuff, zsvsheet_run_buffer_transformation, trn) != 0) {
    // no worker will ever run: release what it would have and unstick the buffer
    nbuff->write_done = 1;
    nbuff->index_ready = 1;
    nbuff->status = NULL;
    free(trn->default_status);
    trn->default_status = NULL;
    if (trn->on_done)
      trn->on_done(trn);
    zsvsheet_transformation_delete(trn); // frees user_context
    zsv_index_commit_rows(index);        // the delete stages the final index row
    return zsvsheet_status_error;
  }
  return stat;

error:
  if (owned_input) { // ownership not yet transferred to trn
    remove(owned_input);
    free(owned_input);
  }

  if (trn && trn->on_done)
    trn->on_done(trn);
  if (trn) {
    zsvsheet_transformation_delete(trn); // frees user_context; may stage rows into index via the writer teardown
  } else {
    free(opts.user_context); // on_done needs a trn; at least reclaim the context itself
    if (zopts.stream)        // ownership never reached the transformation
      fclose(zopts.stream);
  }
  zsv_index_delete(index); // only after the delete above, which writes into it

  return stat;
}
