#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h> // malloc/free
#include <errno.h>  // error reporting

// Define a reasonable buffer size for the buffered copy
#define COPY_BUFFER_SIZE (1024 * 64) // 64KB

#ifdef _WIN32
// Windows target (via mingw64)
#include <io.h>      // _get_osfhandle
#include <windows.h> // HANDLE, ReadFile, WriteFile
#else
// POSIX target (Linux/macOS)
#include <sys/stat.h> // fstat, stat
// #include <sys/uio.h>  // macOS/BSD sendfile() definition
#ifdef __linux__
#include <sys/sendfile.h> // only on Linux
#endif
#endif

// concatenate two files. if possible, use zero-copy via sendfile
long zsv_concatenate_copy(int out_fd, int in_fd, off_t size) {
  long total_written = 0;

#ifdef _WIN32
  // --- windows: buffered copy via native apis
  HANDLE hOut = (HANDLE)_get_osfhandle(out_fd);
  HANDLE hIn = (HANDLE)_get_osfhandle(in_fd);
  if (hOut == INVALID_HANDLE_VALUE || hIn == INVALID_HANDLE_VALUE)
    return -1;

  char *buffer = malloc(COPY_BUFFER_SIZE);
  if (!buffer)
    return -1;

  DWORD bytes_read, bytes_written;
  BOOL result;

  while (total_written < size) {
    DWORD bytes_to_read =
      (DWORD)((size - total_written < COPY_BUFFER_SIZE) ? (size - total_written) : COPY_BUFFER_SIZE);

    result = ReadFile(hIn, buffer, bytes_to_read, &bytes_read, NULL);
    if (!result || bytes_read == 0) {
      free(buffer);
      return -1;
    }

    result = WriteFile(hOut, buffer, bytes_read, &bytes_written, NULL);
    if (!result || bytes_written != bytes_read) {
      free(buffer);
      return -1;
    }

    total_written += bytes_written;
  }

  free(buffer);
  return total_written;

#elif defined(__linux__)
  // --- linux: zero-copy! ---
  off_t offset = 0;
  long bytes_to_copy = size;
  // sendfile: target_fd, source_fd, offset*, count
  long result = sendfile(out_fd, in_fd, &offset, bytes_to_copy);
  return result;

#else
  (void)(size);
  // --- generic posix fallback (buffered copy) ---
  char *buffer = malloc(COPY_BUFFER_SIZE);
  if (!buffer)
    return -1;

  ssize_t read_bytes, write_bytes;
  while ((read_bytes = read(in_fd, buffer, COPY_BUFFER_SIZE)) > 0) {
    write_bytes = write(out_fd, buffer, read_bytes);
    if (write_bytes != read_bytes) {
      free(buffer);
      return -1;
    }
    total_written += write_bytes;
  }
  free(buffer);
  return (read_bytes == 0) ? total_written : -1;
#endif
}
