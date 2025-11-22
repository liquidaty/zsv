#include <errno.h>

unsigned int zsv_get_number_of_cores() {
  long ncores = 1; // Default to 1 in case of failure

#ifdef _WIN32
  // Implementation for Windows (when cross-compiled with mingw64)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  ncores = sysinfo.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
  // Implementation for Linux and macOS (uses POSIX standard sysconf)
  // _SC_NPROCESSORS_ONLN gets the number of *online* processors.
  ncores = sysconf(_SC_NPROCESSORS_ONLN);
#else
  // Fallback for other POSIX-like systems that might not define the symbol
  // or for unexpected compilation environments.
  ncores = 1;
#endif
  // Ensure we return a positive value
  return (unsigned int)(ncores > 0 ? ncores : 1);
}

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
