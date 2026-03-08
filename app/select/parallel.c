#include <errno.h>

static struct zsv_parallel_data *zsv_parallel_data_new(unsigned num_chunks) {
  struct zsv_parallel_data *pdata = calloc(1, sizeof(*pdata));
  if (pdata) {
    pdata->threads = calloc(num_chunks, sizeof(*pdata->threads));
    pdata->chunk_data = calloc(num_chunks, sizeof(*pdata->chunk_data));
    pdata->num_chunks = num_chunks;
    if (pdata->threads && pdata->chunk_data)
      return pdata;
    zsv_parallel_data_delete(pdata);
  }
  // if we got here, we had a memory allocation failure
  errno = ENOMEM;
  return NULL;
}

static void zsv_chunk_data_clear_output(struct zsv_chunk_data *c) {
  if (c) {
#ifdef __linux__
    if (c->tmp_output_filename) {
      unlink(c->tmp_output_filename);
      free(c->tmp_output_filename);
      c->tmp_output_filename = NULL;
    }
#else
    if (c->tmp_f) {
      zsv_memfile_close(c->tmp_f);
      c->tmp_f = NULL;
    }
#endif
  }
}

static void zsv_parallel_data_delete(struct zsv_parallel_data *pdata) {
  if (pdata) {
    for (unsigned int i = 0; i < pdata->num_chunks; i++) {
      if (pdata->chunk_data)
        zsv_chunk_data_clear_output(&pdata->chunk_data[i]);
    }
    free(pdata->threads);
    free(pdata->chunk_data);
    free(pdata);
  }
}
