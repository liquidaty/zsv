#ifndef ZSV_SELECT_PARALLEL_H
#define ZSV_SELECT_PARALLEL_H

#include <sys/types.h> // Required for off_t
#include <pthread.h>   // Required for pthread_t

/**
 * @brief Data structure passed to each worker thread (Chunk 2, 3, 4)
 * Uses in-memory output buffer to avoid I/O lock contention.
 */
struct zsv_chunk_data {
#ifdef ZSV_PARALLEL_TEMPFILE
  char *tmp_output_filename; // temp file + zero-copy sendfile for merge
#else
  void *tmp_f; // zsv_memfile * for in-memory worker output (default)
#endif
  off_t start_offset;
  off_t end_offset; // Stop processing when current offset exceeds this
  off_t actual_next_row_start;
  struct zsv_opts *opts; // Configuration options (read-only)
  enum zsv_status status;
  int id;
  unsigned char skip : 1;
  unsigned char _ : 7;
};

/**
 * @brief Structure to manage thread handles and chunk data for parallel processing.
 */
struct zsv_parallel_data {
  struct zsv_select_data *main_data;
  unsigned num_chunks;
  pthread_t *threads;                // array of N (num_chunks) pointers
  struct zsv_chunk_data *chunk_data; // array of N chunk datas
};

static struct zsv_parallel_data *zsv_parallel_data_new(unsigned num_chunks);
static void zsv_chunk_data_clear_output(struct zsv_chunk_data *c);
static void zsv_parallel_data_delete(struct zsv_parallel_data *pdata);

#endif
