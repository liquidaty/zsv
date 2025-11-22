#ifndef ZSV_CHUNK_H
#define ZSV_CHUNK_H

#include <stddef.h>    // For size_t
#include <stdint.h>    // For uint64_t
#include <sys/types.h> // For off_t

typedef off_t zsv_file_pos;

// Define a struct to hold the (start, end) pair using the standard zsv_file_pos type
struct zsv_chunk_position {
  zsv_file_pos start;
  zsv_file_pos end;
};

/**
 * @brief Calculates N (start, end) file position pairs for a given file,
 * adjusting boundaries to fall immediately after a newline sequence.
 *
 * @param filename       The path to the file.
 * @param N              The desired number of chunks.
 * @param min_size       The minimum size (excluding offset) required for chunking
 * @param initial_offset The initial offset bytes to skip from chunking
 * @return zsv_chunk_position* A dynamically allocated array of N pairs, or NULL on error.
 */
struct zsv_chunk_position *zsv_calculate_file_chunks(const char *filename, uint64_t N,
                                                     uint64_t min_size,
                                                     zsv_file_pos initial_offset);

/**
 * @brief Frees the memory allocated by zsv_calculate_file_chunks. (DRY Cleanup)
 * @param chunks The pointer to the allocated chunk array.
 */
void zsv_free_chunks(struct zsv_chunk_position *chunks);

/**
 * @brief Seeks to a specific offset and reads the first line of text.
 * @param filename The file path.
 * @param offset The starting position to read from (zsv_file_pos).
 * @param buffer Output buffer for the line.
 * @param buf_size Size of the output buffer.
 * @return int 0 on success, -1 on error.
 */
int zsv_read_first_line_at_offset(const char *filename, zsv_file_pos offset, char *buffer, size_t buf_size);

#endif // ZSV_CHUNK_H
