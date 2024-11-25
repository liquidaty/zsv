#include <stdlib.h>

#include "transformation.h"
#include "zsv/utils/file.h"
#include "zsv/utils/prop.h"

struct zsvsheet_transformation {
  zsv_parser parser;
  zsv_csv_writer writer;
  char *output_filename;
  FILE *output_stream;
  unsigned char *output_buffer;
  struct zsvsheet_transformation_opts opts;
  void *user_context;
};

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

  struct zsv_csv_writer_options writer_opts = {0};
  if (!(writer_opts.stream = temp_f = fopen(temp_filename, "w+")))
    goto free;
  if (!(temp_file_writer = zsv_writer_new(&writer_opts)))
    goto free;

  const size_t temp_buff_size = 8196;
  temp_buff = malloc(temp_buff_size);
  if (!temp_buff)
    goto free;
  zsv_writer_set_temp_buff(temp_file_writer, temp_buff, temp_buff_size);
  trn->writer = temp_file_writer;
  trn->output_stream = temp_f;
  trn->output_buffer = temp_buff;
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
