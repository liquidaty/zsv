#include <errno.h>

struct zsv_parallel_data *zsv_parallel_data_new(unsigned num_chunks) {
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

void zsv_parallel_data_delete(struct zsv_parallel_data *pdata) {
  if (pdata) {
    for (int i = 0; i < pdata->num_chunks; i++) {
      if (pdata->chunk_data)
#ifdef __linux__
        free(pdata->chunk_data[i].tmp_output_filename);
#else
        if (pdata->chunk_data[i].tmp_f)
          zsv_memfile_close(pdata->chunk_data[i].tmp_f);
#endif
    }

    free(pdata->threads);
    free(pdata->chunk_data);
    free(pdata);
  }
}
