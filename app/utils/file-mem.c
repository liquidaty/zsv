#include <string.h>
#include <errno.h>

// TO DO: make this a standalone repo

// The structure defining our dual-storage stream
struct zsv_memfile {
  size_t size;          // size of buffer
  size_t used;          // Actual bytes written to memory
  char *tmp_fn;         // name of temp file
  FILE *tmp_f;          // temp FILE *
  size_t total_written; // Total bytes written (memory + disk)
  size_t read_offset;   // Current read position for the entire stream
  bool write_mode;      // Flag to prevent writing after rewind
  char buffer_start;    // start of buffer
};
typedef struct zsv_memfile zsv_memfile;

// --- API Implementation ---
/**
 * Equivalent to fopen: Allocates and initializes the stream.
 */
zsv_memfile *zsv_memfile_open(size_t buffersize) {
  zsv_memfile *zfm = malloc(sizeof(zsv_memfile) + buffersize);
  if (!zfm)
    return NULL;
  memset(zfm, 0, sizeof(zsv_memfile));
  zfm->size = buffersize;
  zfm->write_mode = true;
  return zfm;
}

size_t zsv_memfile_write(const void *data, size_t sz, size_t n, zsv_memfile *zfm) {
  if (!zfm || !zfm->write_mode) {
    errno = EPERM; // Operation not permitted
    return 0;
  }

  size_t nbytes = sz * n;
  if (nbytes == 0)
    return 0;

  const char *data_ptr = (const char *)data;
  size_t remaining_bytes = nbytes;
  size_t written_total = 0;

  // 1. Write to Memory (until full)
  if (zfm->used < zfm->size) {
    size_t mem_space_avail = zfm->size - zfm->used;
    size_t write_to_mem = (remaining_bytes < mem_space_avail) ? remaining_bytes : mem_space_avail;

    memcpy(&zfm->buffer_start + zfm->used, data_ptr, write_to_mem);

    zfm->used += write_to_mem;
    zfm->total_written += write_to_mem;
    data_ptr += write_to_mem;
    remaining_bytes -= write_to_mem;
    written_total += write_to_mem;

    // If memory is now full, transition to disk if data remains
    if (zfm->used == zfm->size && remaining_bytes > 0) {
      // Allocate the temporary disk file. We use tmpfile() for simplicity and security.
      zfm->tmp_f = zsv_tmpfile("zfm_", &zfm->tmp_fn, "wb+");
      if (!zfm->tmp_f) {
        perror("Failed to create temporary file");
        return written_total; // Return what was successfully written to memory
      }
    }
  }

  // 2. Write to Disk (for overflow data)
  if (remaining_bytes > 0 && zfm->tmp_f) {
    size_t written_to_disk = fwrite(data_ptr, 1, remaining_bytes, zfm->tmp_f);

    zfm->total_written += written_to_disk;
    written_total += written_to_disk;
  }

  return written_total;
}

/**
 * Equivalent to freopen (simplified): Switches stream to read-only mode and resets read pointer.
 * Assumes no further writing will occur.
 */
int zsv_memfile_rewind(zsv_memfile *zfm) {
  if (!zfm)
    return -1;

  // Transition to read-only mode
  zfm->write_mode = false;

  // Reset the overall read pointer to the start of the combined stream
  zfm->read_offset = 0;

  // Reset the disk file pointer if it exists, essential for the read logic.
  if (zfm->tmp_f) {
    rewind(zfm->tmp_f);
  }
  return 0;
}

/**
 * Equivalent to fread: Reads data seamlessly from memory and then disk.
 * This function enforces the seamless abstraction layer.
 */
size_t zsv_memfile_read(void *buffer, size_t size, size_t nitems, zsv_memfile *zfm) {
  if (!zfm || zfm->write_mode) {
    // Must call zsv_memfile_rewind() before reading
    errno = EPERM;
    return 0;
  }

  size_t nbytes = size * nitems;
  char *buffer_ptr = (char *)buffer;
  size_t remaining_bytes = nbytes;
  size_t read_total = 0;

  // Total available bytes to read across the entire stream
  size_t available_bytes = zfm->total_written - zfm->read_offset;
  if (available_bytes == 0)
    return 0; // EOF

  // Limit read request to available data
  if (remaining_bytes > available_bytes) {
    remaining_bytes = available_bytes;
  }

  // 1. Read from Memory
  if (zfm->read_offset < zfm->size) {
    // Calculate the starting position within the memory buffer
    size_t mem_start = zfm->read_offset;

    // Calculate how much memory data is left to read
    size_t mem_data_left = zfm->used - mem_start;

    // Calculate how much to read from memory in this call
    size_t read_from_mem = (remaining_bytes < mem_data_left) ? remaining_bytes : mem_data_left;

    if (read_from_mem > 0) {
      memcpy(buffer_ptr, &zfm->buffer_start + mem_start, read_from_mem);

      zfm->read_offset += read_from_mem;
      buffer_ptr += read_from_mem;
      remaining_bytes -= read_from_mem;
      read_total += read_from_mem;
    }
  }

  // 2. Read from Disk (if necessary)
  if (remaining_bytes > 0 && zfm->tmp_f) {
    // Note: The disk file pointer was already managed by zsv_memfile_rewind or is positioned correctly
    // relative to the read_offset shift from step 1.
    size_t read_from_disk = fread(buffer_ptr, 1, remaining_bytes, zfm->tmp_f);

    zfm->read_offset += read_from_disk;
    read_total += read_from_disk;
  }

  return read_total;
}

/**
 * Equivalent to fclose: Cleans up resources.
 */
void zsv_memfile_close(zsv_memfile *zfm) {
  if (!zfm)
    return;

  // Close and implicitly delete the temporary file if it was opened
  if (zfm->tmp_f) {
    fclose(zfm->tmp_f);
    unlink(zfm->tmp_fn);
  }
  free(zfm->tmp_fn);
  zfm->tmp_fn = NULL;

  // Free the main structure
  free(zfm);
}
