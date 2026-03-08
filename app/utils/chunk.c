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
 * @param only_crlf If non-zero, only treat \r\n as a newline.
 * @return zsv_file_pos The position after the newline sequence, or -1 if not found.
 */
static zsv_file_pos zsv_find_chunk_start(FILE *fp, zsv_file_pos initial_offset, zsv_file_pos boundary, int only_crlf) {
  char c;
  // Seek to the initial offset.
  if (fseek(fp, initial_offset, SEEK_SET) != 0) {
    return -1; // Seek error
  }

  // Scan forward for the start of a newline sequence
  while (ftell(fp) < boundary && fread(&c, 1, 1, fp) == 1) {
    if (only_crlf) {
      if (c == '\r') {
        // We found a CR. Check immediately if the next char is LF.
        char next;
        if (ftell(fp) < boundary && fread(&next, 1, 1, fp) == 1) {
          if (next == '\n') {
            // Found \r\n sequence. The chunk starts immediately after.
            return ftell(fp);
          }
          // The next char was NOT \n.
          // We must rewind one byte so the loop processes 'next' correctly
          // (in case 'next' is itself a \r starting a valid sequence).
          fseek(fp, -1, SEEK_CUR);
        }
      }
    } else {
      if (zsv_is_newline(c)) {
        // Found the start of a sequence. Scan past all consecutive newline characters.
        zsv_file_pos position_after_newline = ftell(fp);

        while (position_after_newline < boundary && fread(&c, 1, 1, fp) == 1) {
          if (zsv_is_newline(c)) {
            position_after_newline = ftell(fp); // Keep tracking position past the sequence
          } else {
            // Found the first non-newline character.
            // The new start is at the current position (one byte past the last read)
            // so we return the start of that character (ftell - 1).
            return ftell(fp) - 1;
          }
        }
        // If inner loop breaks due to EOF, return -1
        return -1;
      }
    }
  }

  // Reached EOF/boundary without finding a valid split point
  return -1;
}

static int zsv_read_first_line_at_offset(const char *filename, zsv_file_pos offset, char *buffer, size_t buf_size) {
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

// --- Public Library Implementations ---

struct zsv_chunk_position *zsv_guess_file_chunks(const char *filename, uint64_t N, uint64_t min_size,
                                                 zsv_file_pos initial_offset
#ifndef ZSV_NO_ONLY_CRLF
                                                 ,
                                                 int only_crlf
#endif
) {

#ifdef ZSV_NO_ONLY_CRLF
  int only_crlf = 0;
#endif
  if (N == 0)
    return NULL;

  // Open in binary mode ('rb') is crucial for accurate byte counts.
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror("zsv_guess_file_chunks: Failed to open file");
    return NULL;
  }

  // 1. Get total file size using fstat()
  struct stat st;
  if (fstat(fileno(fp), &st) == -1) {
    perror("zsv_guess_file_chunks: fstat failed");
    fclose(fp);
    return NULL;
  }
  zsv_file_pos total_size = (zsv_file_pos)st.st_size;
  if (total_size < initial_offset) {
    perror("zsv_guess_file_chunks: initial_offset exceeds file size");
    fclose(fp);
    return NULL;
  }
  total_size -= initial_offset;

  if (total_size < (zsv_file_pos)min_size) {
    fprintf(stderr, "file size too small for parallelization\n");
    fclose(fp);
    return NULL;
  }

  // Allocate memory for the N chunk positions
  struct zsv_chunk_position *chunks = (struct zsv_chunk_position *)malloc(N * sizeof(*chunks));
  if (chunks == NULL) {
    perror("zsv_guess_file_chunks: malloc failed");
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
    zsv_file_pos nominal_boundary = (i == N - 1) ? total_size : (zsv_file_pos)((i + 1) * base_size);

    if (i < N - 1) {
      // Adjust the boundary for all but the last chunk
      // Pass the only_crlf flag down to the helper
      zsv_file_pos new_start_offset = zsv_find_chunk_start(fp, nominal_boundary, total_size, only_crlf);

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
      // The last chunk always ends at the total_size - 1 byte
      chunks[i].end = total_size + initial_offset > 0 ? total_size + initial_offset - 1 : 0;
    }

    // Defensive check for inverted start/end
    if (chunks[i].start > chunks[i].end && total_size > 0)
      chunks[i].end = chunks[i].start;
  }

  fclose(fp);
  return chunks;
}

void zsv_free_chunks(struct zsv_chunk_position *chunks) {
  if (chunks) {
    free(chunks);
  }
}

const char *zsv_chunk_status_str(enum zsv_chunk_status stat) {
  switch (stat) {
  case zsv_chunk_status_ok:
    return NULL;
  case zsv_chunk_status_no_file_input:
    return "Parallelization requires a file input";
  case zsv_chunk_status_overwrite:
    return "Parallelization cannot be used with overwrite";
  case zsv_chunk_status_max_rows:
    return "Parallelization cannot be used with -L,--limit-rows";
  }
  return NULL;
}

enum zsv_chunk_status zsv_chunkable(const char *inputpath, struct zsv_opts *opts) {
  if (!inputpath)
    return zsv_chunk_status_no_file_input;
  struct zsv_opt_overwrite o = {0};
  if (memcmp(&opts->overwrite, &o, sizeof(o)) || opts->overwrite_auto)
    return zsv_chunk_status_overwrite;
  if (opts->max_rows)
    return zsv_chunk_status_max_rows;
  return zsv_chunk_status_ok;
}
