#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy

#include "io.h"

/**
 * @brief Creates a directory, supporting paths longer than MAX_PATH.
 *
 * This function uses the Windows API CreateDirectoryW with the \\?\ prefix
 * to reliably create directories even if their absolute path exceeds the
 * traditional MAX_PATH limit.
 *
 * @param path A UTF-8 encoded string representing the absolute path of the
 * directory to create. Relative paths are NOT reliably handled.
 * Forward slashes '/' will be converted to backslashes '\'.
 * @return 0 on success (directory created or already exists).
 * Returns a non-zero Win32 error code on failure (see GetLastError()).
 */

DWORD zsv_mkdir_winlp(const char *path_utf8) {
  if (path_utf8 == NULL || path_utf8[0] == '\0') {
    fprintf(stderr, "Error: Input path is NULL or empty.\n");
    return ERROR_INVALID_PARAMETER; // Or another suitable error code
  }

  if (strlen(path_utf8) < 260) { // try the easy way first
    int rc = mkdir(path_utf8);
    if (!rc || rc == EEXIST)
      return 0;
  }

  wchar_t *finalPath;
  DWORD rc = zsv_pathToPrefixedWidePath(path_utf8, &finalPath);
  if (rc)
    return rc;

  // 4. Call CreateDirectoryW
  BOOL success = CreateDirectoryW(finalPath, NULL); // NULL for default security attributes

  DWORD lastError = 0;
  if (!success) {
    lastError = GetLastError();
    // It's okay if the directory already exists
    if (lastError == ERROR_ALREADY_EXISTS) {
      // printf("Debug: Directory already exists (considered success).\n");
      lastError = 0; // Treat as success
    } else {
      fprintf(stderr, "Error: CreateDirectoryW failed (%lu) for path: %ls\n", lastError, finalPath);
    }
  } else {
    // printf("Debug: CreateDirectoryW succeeded for path: %ls\n", finalPath);
    lastError = 0; // Success
  }

  free(finalPath);

  return lastError; // Return 0 on success (or already exists), else the error code
}

#ifdef DIRS_MKDIR_TEST
#define FILESLASH '\\'

#include "win/io.c"
#include "dirs_exists_longpath.c"
/**
 * Check if a directory exists
 * return true (non-zero) or false (zero)
 */
int zsv_dir_exists(const char *path) {
#ifdef WIN32
  if (strlen(path) >= MAX_PATH)
    return zsv_dir_exists_winlp(path);

  // TO DO: support win long filepath prefix
  // TO DO: work properly if dir exists but we don't have permission
  wchar_t wpath[MAX_PATH];
  mbstowcs(wpath, path, MAX_PATH);

  DWORD attrs = GetFileAttributesW(wpath);
  if (attrs == INVALID_FILE_ATTRIBUTES)
    // Could check GetLastError() to see if it's a permission issue vs. not-found
    return 0;

  // If it has the directory attribute, it's presumably a directory
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

#else
  struct stat path_stat;
  if (!stat(path, &path_stat))
    return S_ISDIR(path_stat.st_mode);
  return 0;
#endif
}

/**
 * Make a directory, as well as any intermediate dirs
 * return zero on success
 */
#ifdef WIN32
#define zsv_mkdir zsv_mkdir_winlp
#else
#define zsv_mkdir mkdir
#endif
int zsv_mkdirs(const char *path, char path_is_filename) {
  // int rc = 0;
  if (!path || !*path)
    return -1;
  size_t len = strlen(path);

  /*
#ifdef WIN32
  // TO DO: handle windows long-file prefix "\\?\"
  // for now, explicitly do not handle
  if (len > 2 && path[2] == '?')
    fprintf(stderr, "Invalid path (long file prefix not supported): %s\n", path);
#endif
  */
  if (len < 1) //  || len > FILENAME_MAX)
    return -1;

  char *tmp = strdup(path);
  if (!tmp) {
    perror(path);
    return -1;
  }

  if (len && strchr("/\\", tmp[len - 1]))
    tmp[--len] = 0;

  int offset = 0;
#ifdef WIN32
  if (len > 1) {
    // starts with two slashes
    if (strchr("/\\", tmp[0]) && strchr("/\\", tmp[1])) {
      offset = 2;
      // find the next slash
      char *path_end = tmp + 3;
      while (*path_end && !strchr("/\\", *path_end))
        path_end++;
      if (*path_end)
        path_end++;
      if (*path_end)
        offset = path_end - tmp;
      else {
        fprintf(stderr, "Invalid path: %s\n", path);
        free(tmp);
        return -1;
      }
    }
    // starts with *:
    else if (tmp[1] == ':')
      offset = 2;
  }
#else
  offset = 1;
#endif

  // TO DO: first find the longest subdir that exists, in *reverse* order so as
  // to properly handle case where no access to intermediate dir,
  // and then only start mkdir from there
  int last_dir_exists_rc = 0;
  int last_errno = -1;
  for (char *p = tmp + offset; /* !rc && */ *p; p++) {
    if (strchr("/\\", *p)) {
      char tmp_c = p[1];
      p[0] = FILESLASH;
      p[1] = '\0';
      if (*tmp && !(last_dir_exists_rc = zsv_dir_exists(tmp))) {
        if (zsv_mkdir(tmp
#ifndef WIN32
                      ,
                      S_IRWXU
#endif
                      )) {
          if (errno == EEXIST)
            last_dir_exists_rc = 1;
          else { // errno could be EEXIST if we have no permissions to an intermediate directory
            last_errno = errno;
            perror(tmp);
            //          rc = -1;
          }
        } else
          last_dir_exists_rc = 1;
      }
      p[1] = tmp_c;
    }
  }

  if (/* !rc && */ path_is_filename == 0 && *tmp && !(last_dir_exists_rc = zsv_dir_exists(tmp))) {
    if (zsv_mkdir(tmp
#ifndef WIN32
                  ,
                  S_IRWXU
#endif
                  )) {
      if (errno == EEXIST)
        last_dir_exists_rc = 1;
      else {
        last_errno = errno;
        perror(tmp);
        // rc = -1;
      }
    } else
      last_dir_exists_rc = 1;
  }

  free(tmp);
  return last_dir_exists_rc ? 0 : last_errno ? last_errno : -1;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <absolute_directory_path>\n", argv[0]);
    fprintf(stderr, "Example: %s \"C:\\Temp\\My Very Long Directory Name That Exceeds 260 Characters\\Subfolder\"\n",
            argv[0]);
    fprintf(stderr, "       %s \"\\\\?\\C:\\Temp\\Another Long Path\\Subfolder\"\n", argv[0]);
    fprintf(stderr, "Note: Provide an ABSOLUTE path.\n");
    return 1;
  }

  const char *targetPath = argv[1];

  printf("Attempting to create directory: %s\n", targetPath);

  DWORD result = zsv_mkdirs(targetPath, 0);

  if (result == 0) {
    printf("Success: Directory created or already exists.\n");
    return 0;
  } else {
    // You can provide more detailed error messages by checking specific Win32 error codes
    // For example: if (result == ERROR_PATH_NOT_FOUND) { ... }
    fprintf(stderr, "Error: Failed to create directory %s. Win32 Error Code: %lu\n", targetPath, result);

    // Optionally print the system error message for the code
    LPSTR messageBuffer = NULL;
    size_t size =
      FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                     result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    if (messageBuffer) {
      fprintf(stderr, "System Message: %s\n", messageBuffer);
      LocalFree(messageBuffer); // Free buffer allocated by FormatMessage
    } else {
      fprintf(stderr, "Could not format error message for code %lu.\n", result);
    }

    return 1; // Indicate failure
  }
}

#endif // DIRS_MKDIR_TEST
