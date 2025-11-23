// /src/app/utils/chunk.c: implements /src/app/utils/chunk.h

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"

/**
 * @brief Checks if a character is a newline character ('\n' or '\r').
 * @param c The character to check.
 * @return int 1 if newline, 0 otherwise.
 */
static int zsv_is_newline(char c) {
  return (c == '\n' || c == '\r');
}

/**
 * @brief Scans forward from an initial offset to find the first position after a newline sequence.
 *
 * @param fp The open file pointer.
 * @param initial_offset The starting point of the search (nominal boundary).
 * @param boundary The absolute maximum file size (total_size).
 * @return zsv_file_pos The position after the newline sequence, or -1 if the end of file is reached without a newline.
 */
static zsv_file_pos zsv_find_chunk_start(FILE *fp, zsv_file_pos initial_offset, zsv_file_pos boundary) {
  char c;

  // 1. Seek to the initial offset. Use fseek and check return for error handling.
  if (fseek(fp, initial_offset, SEEK_SET) != 0) {
    return -1; // Seek error
  }

  // 2. Scan forward for the start of a newline sequence
  // Use ftell for current position, which returns long (or __int64 on MinGW with off_t)
  while (ftell(fp) < boundary && fread(&c, 1, 1, fp) == 1) {
    if (zsv_is_newline(c)) {
      // Found the start of a sequence. Scan past all consecutive newline characters.
      zsv_file_pos position_after_newline = ftell(fp);

      // Handle sequence (e.g., \n\r, \r\n, \n\n\n)
      while (position_after_newline < boundary && fread(&c, 1, 1, fp) == 1) {
        if (zsv_is_newline(c)) {
          position_after_newline = ftell(fp); // Keep tracking position past the sequence
        } else {
          // Found the first non-newline character
          // The new start is at the current position, which is one byte past the last read
          return ftell(fp) - 1;
        }
      }

      // If the inner loop breaks due to EOF/boundary, the file ends with a newline sequence.
      return -1; // Signal that the file ends prematurely or adjustment failed
    }
  }

  // Reached EOF/boundary without finding a valid split point
  return -1;
}

// --- Public Library Implementations ---

struct zsv_chunk_position *zsv_calculate_file_chunks(const char *filename, uint64_t N, uint64_t min_size,
                                                     zsv_file_pos initial_offset) {
  if (N == 0)
    return NULL;

  // Open in binary mode ('rb') is crucial for accurate byte counts on all platforms, including Windows/MinGW.
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror("zsv_calculate_file_chunks: Failed to open file");
    return NULL;
  }

  // 1. Get total file size using fstat() on the file descriptor
  struct stat st;
  if (fstat(fileno(fp), &st) == -1) {
    perror("zsv_calculate_file_chunks: fstat failed");
    fclose(fp);
    return NULL;
  }
  zsv_file_pos total_size = (zsv_file_pos)st.st_size;
  if (total_size < initial_offset) {
    perror("zsv_calculate_file_chunks: ftell failed");
    fclose(fp);
    return NULL;
  }
  total_size -= initial_offset;

  if (total_size < min_size) {
    fprintf(stderr, "file size too small for parallelization\n");
    fclose(fp);
    return NULL;
  }

  // Allocate memory for the N chunk positions
  struct zsv_chunk_position *chunks = (struct zsv_chunk_position *)malloc(N * sizeof(*chunks));
  if (chunks == NULL) {
    perror("zsv_calculate_file_chunks: malloc failed");
    fclose(fp);
    return NULL;
  }

  if (initial_offset)
    fseek(fp, initial_offset, SEEK_SET);

  zsv_file_pos base_size = total_size / N;
  zsv_file_pos current_offset = initial_offset;

  for (uint64_t i = 0; i < N; ++i) {
    chunks[i].start = current_offset;

    // Calculate the initial nominal boundary for this chunk
    zsv_file_pos nominal_boundary = (i == N - 1) ? total_size : (i + 1) * base_size;

    if (i < N - 1) {
      // Adjust the boundary for all but the last chunk
      zsv_file_pos new_start_offset = zsv_find_chunk_start(fp, nominal_boundary, total_size);

      if (new_start_offset < 0) {
        // Warning: Could not find a valid split after nominal boundary
        // We use the nominal boundary, which might break a line
        chunks[i].end = nominal_boundary - 1;
        current_offset = nominal_boundary;
      } else {
        chunks[i].end = new_start_offset - 1;
        current_offset = new_start_offset;
      }
    } else {
      // The last chunk always ends at the total_size - 1 byte (or 0 if size is 0)
      chunks[i].end = total_size + initial_offset > 0 ? total_size + initial_offset - 1 : 0;
    }

    // Defensive check for inverted start/end
    if (chunks[i].start > chunks[i].end && total_size > 0)
      chunks[i].end = chunks[i].start; // Adjust to a single byte or start
  }

  fclose(fp);
  return chunks;
}

void zsv_free_chunks(struct zsv_chunk_position *chunks) {
  if (chunks) {
    free(chunks);
  }
}

int zsv_read_first_line_at_offset(const char *filename, zsv_file_pos offset, char *buffer, size_t buf_size) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror("zsv_read_first_line_at_offset: Failed to open file");
    return -1;
  }

  if (offset < 0 || fseek(fp, offset, SEEK_SET) != 0) {
    fprintf(stderr, "zsv_read_first_line_at_offset: Error: Invalid offset or fseek failed at %lld\n",
            (long long)offset);
    fclose(fp);
    return -1;
  }

  // Use fgets. It handles both \n and \r\n line endings appropriately.
  if (fgets(buffer, (int)buf_size, fp) == NULL) {
    if (feof(fp)) {
      buffer[0] = '\0'; // Empty chunk
    } else {
      perror("zsv_read_first_line_at_offset: fgets failed");
      fclose(fp);
      return -1;
    }
  }

  // Remove the trailing newline sequence (CRLF or LF) for clean output (DRY cleanup logic)
  size_t len = strlen(buffer);
  if (len > 0) {
    // Check for LF
    if (buffer[len - 1] == '\n') {
      buffer[--len] = '\0';
    }
    // Check for CR (handles both bare CR and the CR in CRLF)
    if (len > 0 && buffer[len - 1] == '\r') {
      buffer[len - 1] = '\0';
    }
  }

  fclose(fp);
  return 0;
}
