#include <stdlib.h>
#include <string.h>

#include "handlers_internal.h"
#include "transformation.h"
#include "pthread.h"
#include "zsv/utils/file.h"
#include "zsv/utils/prop.h"

struct zsvsheet_transformation {
  zsv_parser parser;
  zsv_csv_writer writer;
  char *output_filename;
  FILE *output_stream;
  unsigned char *output_buffer;
  int output_fileno;
  size_t output_count;
  struct zsvsheet_transformation_opts opts;
  void *user_context;

  struct zsvsheet_ui_buffer *ui_buffer;
  char *default_status;
  void (*on_done)(zsvsheet_transformation trn);
};

static size_t transformation_write(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream) {
  struct zsvsheet_transformation *trn = stream;

  const size_t count = fwrite(ptr, size, nitems, trn->output_stream);
  trn->output_count += count;

  return count > 0 ? count : 0;
}

enum zsv_status zsvsheet_transformation_new(struct zsvsheet_transformation_opts opts, zsvsheet_transformation *out) {
  unsigned char *temp_buff = NULL;
  char *temp_filename = NULL;
  FILE *temp_f = NULL;
  zsv_csv_writer temp_file_writer = NULL;
  enum zsv_status zst = zsv_status_memory;

  struct zsvsheet_transformation *trn = calloc(1, sizeof(*trn));
  if (trn == NULL)
    return zst;

  trn->on_done = opts.on_done;
  trn->ui_buffer = opts.ui_buffer;
  zst = zsv_status_error;

  temp_filename = zsv_get_temp_filename("zsvsheet_filter_XXXXXXXX");
  if (!temp_filename)
    goto free;
  trn->output_filename = temp_filename;

  if (!(temp_f = fopen(temp_filename, "w+")))
    goto free;
  if (setvbuf(temp_f, NULL, _IONBF, 0))
    goto free;
  trn->output_stream = temp_f;

  struct zsv_csv_writer_options writer_opts = {
    .with_bom = 0,
    .write = transformation_write,
    .stream = trn,
    .table_init = NULL,
    .table_init_ctx = NULL,
  };
  if (!(temp_file_writer = zsv_writer_new(&writer_opts)))
    goto free;

  const size_t temp_buff_size = 2 * 1024 * 1024;
  temp_buff = malloc(temp_buff_size);
  if (!temp_buff)
    goto free;
  trn->output_buffer = temp_buff;
  zsv_writer_set_temp_buff(temp_file_writer, temp_buff, temp_buff_size);
  trn->writer = temp_file_writer;

  trn->user_context = opts.zsv_opts.ctx;
  opts.zsv_opts.ctx = trn;

  zst = zsv_new_with_properties(&opts.zsv_opts, opts.custom_prop_handler, opts.input_filename, &trn->parser);
  if (zst != zsv_status_ok)
    goto free;

  *out = trn;
  return zst;

free:
  if (trn)
    free(trn);
  if (temp_filename)
    free(temp_filename);
  if (temp_f)
    fclose(temp_f);
  if (temp_file_writer)
    zsv_writer_delete(temp_file_writer);
  if (temp_buff)
    free(temp_buff);

  return zst;
}

void zsvsheet_transformation_delete(zsvsheet_transformation trn) {
  zsv_writer_delete(trn->writer);
  zsv_delete(trn->parser);
  free(trn->output_filename);
  fclose(trn->output_stream);
  free(trn->output_buffer);
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

  size_t c = trn->output_count;
  char cancelled = 0;
  while (!cancelled && (zst = zsv_parse_more(parser)) == zsv_status_ok) {
    pthread_mutex_lock(mutex);
    cancelled = uib->worker_cancelled;
    if (trn->output_count != c)
      uib->write_progressed = 1;
    pthread_mutex_unlock(mutex);
  }

  if (zst == zsv_status_no_more_input || zst == zsv_status_cancelled)
    zsv_finish(parser);

  if (trn->on_done)
    trn->on_done(trn);

  pthread_mutex_lock(mutex);
  char *buff_status_old = uib->status;
  uib->write_progressed = 1;
  uib->write_done = 1;
  if (buff_status_old == trn->default_status)
    uib->status = NULL;
  pthread_mutex_unlock(mutex);

  if (buff_status_old == trn->default_status)
    free(buff_status_old);
  if (trn->user_context)
    free(trn->user_context);
  zsvsheet_transformation_delete(trn);

  return NULL;
}

enum zsvsheet_status zsvsheet_push_transformation(zsvsheet_proc_context_t ctx,
                                                  struct zsvsheet_buffer_transformation_opts opts) {
  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  const char *filename = zsvsheet_buffer_data_filename(buff);
  enum zsvsheet_status stat = zsvsheet_status_error;
  struct zsvsheet_buffer_info_internal info = zsvsheet_buffer_info_internal(buff);

  // TODO: Starting a second transformation before the first ends works, but if the second is faster
  //       than the first then it can end prematurely and read a partially written row.
  //       We could override the input stream reader to wait for more data when it sees EOF
  if (info.write_in_progress && !info.write_done)
    return zsvsheet_status_busy;

  // TODO: custom_prop_handler is not passed to extensions?
  struct zsvsheet_transformation_opts trn_opts = {
    .custom_prop_handler = NULL,
    .input_filename = filename,
    .on_done = opts.on_done,
    .ui_buffer = NULL,
  };
  zsvsheet_transformation trn = NULL;
  struct zsv_opts zopts = zsvsheet_buffer_get_zsv_opts(buff);

  zopts.ctx = opts.user_context;
  zopts.row_handler = (void (*)(void *))opts.row_handler;
  zopts.stream = fopen(filename, "rb");

  if (!zopts.stream)
    goto out;

  trn_opts.zsv_opts = zopts;

  enum zsv_status zst = zsvsheet_transformation_new(trn_opts, &trn);
  if (zst != zsv_status_ok)
    return stat;

  // Transform part of the file to initially populate the UI buffer
  // TODO: If the transformation is a reduction that doesn't output for some time this will caus a pause
  zsv_parser parser = zsvsheet_transformation_parser(trn);
  while ((zst = zsv_parse_more(parser)) == zsv_status_ok) {
    if (trn->output_count > 0)
      break;
  }

  switch (zst) {
  case zsv_status_no_more_input:
  case zsv_status_cancelled:
    if (zsv_finish(parser) != zsv_status_ok)
      goto out;
    zsv_writer_flush(trn->writer);
    break;
  case zsv_status_ok:
    break;
  default:
    goto out;
  }

  struct zsvsheet_ui_buffer_opts uibopts = {0};

  uibopts.data_filename = zsvsheet_transformation_filename(trn);
  uibopts.write_after_open = 1;

  stat = zsvsheet_open_file_opts(ctx, &uibopts);
  if (stat != zsvsheet_status_ok)
    goto out;

  struct zsvsheet_ui_buffer *nbuff = zsvsheet_buffer_current(ctx);
  trn->ui_buffer = nbuff;
  nbuff->write_progressed = 1;

  if (zst != zsv_status_ok) {
    nbuff->write_done = 1;
    goto out;
  }

  asprintf(&trn->default_status, "(working) Press ESC to cancel ");
  nbuff->status = trn->default_status;

  zsvsheet_ui_buffer_create_worker(nbuff, zsvsheet_run_buffer_transformation, trn);
  return stat;

out:
  if (trn && trn->on_done)
    opts.on_done(trn);
  if (trn)
    zsvsheet_transformation_delete(trn);

  return stat;
}
