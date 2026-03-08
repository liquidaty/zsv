#ifndef ZSV_CHUNK_H
#define ZSV_CHUNK_H

#include <zsv/common.h> // struct zsv_opts
#include <stddef.h>     // For size_t
#include <stdint.h>     // For uint64_t
#include <sys/types.h>  // For off_t

typedef off_t zsv_file_pos;

// Define a struct to hold the (start, end) pair using the standard zsv_file_pos type
struct zsv_chunk_position {
  zsv_file_pos start;
  zsv_file_pos end;
};

/**
 * @brief Divide a file into N chunks for parallel processing.
 *
 * Scans the file to find N approximately equal sections, ensuring that
 * chunk boundaries align with newline sequences so rows are not split.
 *
 * @param filename Path to the file to be chunked.
 * @param N The target number of chunks.
 * @param min_size The minimum file size required to attempt parallelization.
 * @param initial_offset The byte offset to start chunking from (usually 0).
 * @param only_crlf If non-zero, boundaries are split strictly on \r\n sequences.
 * If zero, \r or \n are accepted as boundaries.
 * @return struct zsv_chunk_position* An array of N chunk positions (must be freed by caller),
 * or NULL if the file cannot be chunked or an error occurs.
 */
struct zsv_chunk_position *zsv_guess_file_chunks(const char *filename, uint64_t N, uint64_t min_size,
                                                 zsv_file_pos initial_offset
#ifndef ZSV_NO_ONLY_CRLF
                                                 ,
                                                 int only_crlf
#endif
);

/**
 * @brief Frees the memory allocated by zsv_guess_file_chunks. (DRY Cleanup)
 * @param chunks The pointer to the allocated chunk array.
 */
void zsv_free_chunks(struct zsv_chunk_position *chunks);

enum zsv_chunk_status {
  zsv_chunk_status_ok = 0,
  zsv_chunk_status_no_file_input,
  zsv_chunk_status_overwrite,
  zsv_chunk_status_max_rows
};

/**
 * zsv_chunkable(): check if chunking is compatible wth options; return chunk_status
 */
enum zsv_chunk_status zsv_chunkable(const char *inputpath, struct zsv_opts *opts);

/**
 * Convert zsv_chunk_status to string description
 */
const char *zsv_chunk_status_str(enum zsv_chunk_status stat);

#endif // ZSV_CHUNK_H
