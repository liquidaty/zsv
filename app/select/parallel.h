#include <sys/types.h> // Required for off_t
#include <pthread.h>   // Required for pthread_t

#define NUM_CHUNKS 4 // Define the constant here for struct visibility

/**
 * @brief Data structure passed to each worker thread (Chunk 2, 3, 4)
 * Uses in-memory output buffer to avoid I/O lock contention.
 */
struct zsv_chunk_data {
  char *tmp_output_filename;
    off_t start_offset;
    off_t end_offset;       // Stop processing when current offset exceeds this
    struct zsv_opts *opts;  // Configuration options (read-only)
    enum zsv_status status;
  int id;
};

/**
 * @brief Structure to manage thread handles and chunk data for parallel processing.
 */
struct zsv_parallel_data {
    struct zsv_select_data *main_data;
    pthread_t threads[NUM_CHUNKS - 1]; // Handles for the 3 worker threads
    struct zsv_chunk_data chunk_data[NUM_CHUNKS]; // Data for all 4 chunks
};
