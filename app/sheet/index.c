#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/index.h>
#include <zsv/utils/file.h>
#include <zsv/utils/writer.h>

#include "index.h"
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

  if (optsp->uib->data_filename) {
    ix_zopts.stream = fopen(optsp->uib->data_filename, "rb");
  } else {
    ix_zopts.stream = fopen(optsp->filename, "rb");
  }

  if (!ix_zopts.stream)
    goto out;

  ix_zopts.ctx = &ixr;
  ix_zopts.row_handler = build_memory_index_row_handler;

  enum zsv_status zst = zsv_new_with_properties(&ix_zopts, optsp->custom_prop_handler, optsp->filename, &ixr.parser);
  if (zst != zsv_status_ok)
    goto out;

  ixr.ix = zsv_index_new();
  if (!ixr.ix)
    goto out;

  char cancelled = 0;
  while (!cancelled && (zst = zsv_parse_more(ixr.parser)) == zsv_status_ok) {
    pthread_mutex_lock(&optsp->uib->mutex);
    if (optsp->uib->worker_cancelled) {
      cancelled = 1;
      zst = zsv_status_cancelled;
    }
    pthread_mutex_unlock(&optsp->uib->mutex);
  }

  zsv_finish(ixr.parser);

  if (zst == zsv_status_no_more_input || zst == zsv_status_cancelled) {
    ret = zsv_index_status_ok;
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
