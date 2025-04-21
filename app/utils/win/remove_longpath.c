#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <stdio.h>
#include <stdlib.h> // Required for malloc, free
#include <string.h>
#include <wchar.h> // Required for wide character functions

#include "io.h"
#include <errno.h>

// Function to map common Windows error codes to POSIX errno values
// return 0 if could not map
int windows_error_to_errno(DWORD windows_error_code) {
  switch (windows_error_code) {
  case ERROR_FILE_NOT_FOUND:
    return ENOENT;
  case ERROR_PATH_NOT_FOUND:
    return ENOENT;
  case ERROR_INVALID_DRIVE:
    return ENODEV; // Or ENOENT

  case ERROR_ACCESS_DENIED:
    return EACCES;
  case ERROR_INVALID_ACCESS:
    return EACCES; // Or EPERM depending on context
  case ERROR_SHARING_VIOLATION:
    return EACCES; // File is locked/in use
  case ERROR_INVALID_HANDLE:
    return EBADF;
  case ERROR_INVALID_DATA:
    return EILSEQ; // Or EINVAL
  case ERROR_INVALID_PARAMETER:
    return EINVAL;
  case ERROR_NEGATIVE_SEEK:
    return EINVAL;
  case ERROR_WRITE_PROTECT:
    return EROFS; // Read-only filesystem or media
  case ERROR_DISK_FULL:
    return ENOSPC; // No space left on device
  case ERROR_ALREADY_EXISTS:
    return EEXIST;
  case ERROR_FILE_EXISTS:
    return EEXIST;
  case ERROR_TOO_MANY_OPEN_FILES:
    return EMFILE;
  case ERROR_DIRECTORY:
    return ENOTDIR; // Attempted file op on directory (approx)
  case ERROR_BROKEN_PIPE:
    return EPIPE;
  case ERROR_PIPE_NOT_CONNECTED:
    return EPIPE;
  case ERROR_WAIT_NO_CHILDREN:
    return ECHILD; // Wait functions
  case ERROR_CHILD_NOT_COMPLETE:
    return ECHILD; // Wait functions
  case ERROR_SUCCESS:
    return 0; // Not really an error
  default:
    return 0;
  }
}

int zsv_remove_winlp(const char *path_utf8) {
  if (path_utf8 == NULL || path_utf8[0] == '\0') {
    return 0; // Invalid or empty path
  }

  wchar_t *path_to_use = NULL;
  DWORD rc = zsv_pathToPrefixedWidePath(path_utf8, &path_to_use);
  if (!rc) {
    if (!DeleteFileW(path_to_use)) {
      DWORD lastError = GetLastError();
#ifndef NDEBUG
      fprintf(stderr, "Error deleting file '%ls': %lu\n", path_to_use, lastError);
#endif
      if (windows_error_to_errno(lastError)) {
        rc = windows_error_to_errno(lastError);
        errno = rc;
      } else {
        fprintf(stderr, "Unable to delete file '%ls': %lu\n", path_to_use, GetLastError());
        LPSTR messageBuffer = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
        if (messageBuffer) {
          fprintf(stderr, "Error message: %s\n", messageBuffer);
          LocalFree(messageBuffer);
        } else
          fprintf(stderr, "Could not format error message for code %lu.\n", lastError);
        errno = rc = -1;
      }
    }
  }

  free(path_to_use);
  return rc;
}
