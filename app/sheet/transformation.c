#include <stdlib.h>
#include <string.h>

#include "transformation.h"
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

  zst = zsv_status_error;

  temp_filename = zsv_get_temp_filename("zsvsheet_filter_XXXXXXXX");
  if (!temp_filename)
    return zst;
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

  const size_t temp_buff_size = 8192;
  temp_buff = malloc(temp_buff_size);
  if (!temp_buff)
    goto free;
  trn->output_buffer = temp_buff;
  zsv_writer_set_temp_buff(temp_file_writer, temp_buff, temp_buff_size);
  trn->writer = temp_file_writer;

  trn->user_context = opts.zsv_opts.ctx;
  opts.zsv_opts.ctx = trn;

  zst = zsv_new_with_properties(&opts.zsv_opts, opts.custom_prop_handler, opts.input_filename, NULL, &trn->parser);
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
