#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // For struct stat definition (might need MinGW or similar)
#include <time.h>     // For time_t
#include "io.h"

char *zsv_ensureLongPathPrefix(const char *original_path, unsigned char always_prefix);

// Helpers
static WCHAR *utf8_to_wide(const char *utf8_str);
static char *wide_to_utf8(const WCHAR *wide_str);
static void filetime_to_time_t(const FILETIME *ft, time_t *t);
static void populate_stat_from_find_data(struct stat *s, const WIN32_FIND_DATAW *findData);

// Windows-specific implementation
static int zsv_foreach_dirent_aux(const char *dir_path_utf8, size_t depth, size_t max_depth,
                                  zsv_foreach_dirent_handler handler, void *ctx, char verbose) {
  int err = 0;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  WCHAR *search_path_wide = NULL;
  WCHAR *dir_path_wide = NULL;
  char *prefixed_dir_path_utf8 = NULL; // For potential long path

  if (!dir_path_utf8)
    return 1; // Invalid input path

  if (max_depth > 0 && depth >= max_depth)
    return 0; // Max depth reached

  // 1. Handle potential long path prefix (result must be freed)
  prefixed_dir_path_utf8 = zsv_ensureLongPathPrefix(dir_path_utf8, 1);
  const char *prefixed_dir_to_use = prefixed_dir_path_utf8 ? prefixed_dir_path_utf8 : dir_path_utf8;

  // 2. Convert the (potentially prefixed) UTF-8 path to WCHAR (UTF-16) for Windows API
  dir_path_wide = utf8_to_wide(prefixed_dir_to_use);
  if (!dir_path_wide) {
    fprintf(stderr, "Failed to convert path to wide char: %s\n", prefixed_dir_to_use);
    free(prefixed_dir_path_utf8);
    return 1;
  }

  // 3. Create the search pattern (e.g., "C:\\path\\*")
  size_t dir_len = wcslen(dir_path_wide);
  // Need space for path, backslash (optional), wildcard, and null terminator
  search_path_wide = (WCHAR *)malloc((dir_len + 3) * sizeof(WCHAR));
  if (!search_path_wide) {
    fprintf(stderr, "Out of memory allocating search path!\n");
    err = 1;
    goto cleanup;
  }
  wcscpy_s(search_path_wide, dir_len + 3, dir_path_wide);
  // Add trailing backslash if needed (robust check)
  if (dir_len > 0 && search_path_wide[dir_len - 1] != L'\\' && search_path_wide[dir_len - 1] != L'/') {
    search_path_wide[dir_len] = L'\\';
    search_path_wide[dir_len + 1] = L'\0'; // Ensure null termination before appending wildcard
  }
  wcscat_s(search_path_wide, dir_len + 3, L"*"); // Append wildcard

  // 4. Start finding files
  WIN32_FIND_DATAW findData;
  hFind = FindFirstFileW(search_path_wide, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    // Check if the error is simply "directory not found" or similar benign cases
    DWORD dwError = GetLastError();
    if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND && dwError != ERROR_NO_MORE_FILES) {
      // Report more serious errors
      fprintf(stderr, "FindFirstFileW failed for %ls (Error %lu)\n", search_path_wide, dwError);
      err = 1;
      // Maybe add more specific error handling here if needed
    }
    // Otherwise, it's okay (e.g., empty dir or dir doesn't exist), just return
    goto cleanup;
  }

  // 5. Iterate through directory entries
  do {
    // Skip "." and ".." entries
    if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
      continue;
    }

    char *entry_name_utf8 = NULL;
    char *full_path_utf8 = NULL;

    // 6. Convert the found entry name (WCHAR) back to UTF-8
    entry_name_utf8 = wide_to_utf8(findData.cFileName);
    if (!entry_name_utf8) {
      fprintf(stderr, "Failed to convert entry name to UTF-8\n");
      err = 1;
      // Attempt to continue with the next file? Or break? Let's try continuing.
      continue;
    }

    // 7. Construct the full path IN UTF-8 using the ORIGINAL dir_path_utf8
    // Need space for original path, separator, entry name, null terminator
    size_t original_path_len = strlen(dir_path_utf8);
    size_t entry_name_len = strlen(entry_name_utf8);
    size_t full_path_len_needed = original_path_len + 1 + entry_name_len + 1;
    full_path_utf8 = (char *)malloc(full_path_len_needed);

    if (!full_path_utf8) {
      fprintf(stderr, "Out of memory allocating full path!\n");
      free(entry_name_utf8);
      err = 1;
      // If we can't construct the path, we probably should stop.
      break;
    }

    strcpy_s(full_path_utf8, full_path_len_needed, dir_path_utf8);
    // Append backslash if needed
    if (original_path_len > 0 && dir_path_utf8[original_path_len - 1] != '\\' &&
        dir_path_utf8[original_path_len - 1] != '/') {
      strcat_s(full_path_utf8, full_path_len_needed, "\\");
    }
    strcat_s(full_path_utf8, full_path_len_needed, entry_name_utf8);

    // 8. Prepare the handler structure
    struct zsv_foreach_dirent_handle h = {0};
    h.verbose = verbose;
    h.parent = dir_path_utf8; // Use original non-prefixed path for handler
    h.entry = entry_name_utf8;
    h.parent_and_entry = full_path_utf8;
    h.ctx = ctx;
    h.is_dir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

    // Populate the stat structure (best effort)
    populate_stat_from_find_data(&h.stat, &findData);

    // 9. Call the handler
    if (handler) {
      // Handler might set h.no_recurse
      handler(&h, depth + 1);
    }

    // 10. Recurse if it's a directory and recursion is not disabled
    if (h.is_dir && !h.no_recurse) {
      int recurse_err = zsv_foreach_dirent_aux(full_path_utf8, depth + 1, max_depth, handler, ctx, verbose);
      if (recurse_err) {
        err = recurse_err; // Propagate error
                           // Potentially break here if recursive errors should stop processing
      }
    }

    // 11. Cleanup per-entry allocations
    free(entry_name_utf8);
    free(full_path_utf8);

    if (err && !h.is_dir) { // Option: break if a non-recursive error occurred
                            // break;
    }

  } while (FindNextFileW(hFind, &findData) != 0);

  // Check for errors after the loop (FindNextFileW returns 0 on end or error)
  DWORD dwError = GetLastError();
  if (dwError != ERROR_NO_MORE_FILES) {
    fprintf(stderr, "FindNextFileW failed (Error %lu)\n", dwError);
    err = 1;
  }

cleanup:
  // 12. Final Cleanup
  if (hFind != INVALID_HANDLE_VALUE) {
    FindClose(hFind);
  }
  free(search_path_wide);
  free(dir_path_wide);
  free(prefixed_dir_path_utf8); // Free the path possibly returned by zsv_ensureLongPathPrefix

  return err;
}

// --- Helper Function Implementations ---

// Converts a UTF-8 string to a newly allocated WCHAR (UTF-16) string.
// Returns NULL on failure. Caller must free the returned pointer.
static WCHAR *utf8_to_wide(const char *utf8_str) {
  if (!utf8_str)
    return NULL;
  int chars_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
  if (chars_needed <= 0) {
    // fprintf(stderr, "MultiByteToWideChar (pre-flight) failed with error %lu\n", GetLastError());
    return NULL;
  }
  WCHAR *wide_str = (WCHAR *)malloc(chars_needed * sizeof(WCHAR));
  if (!wide_str) {
    // fprintf(stderr, "Failed to allocate memory for wide string\n");
    return NULL;
  }
  int chars_converted = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_str, chars_needed);
  if (chars_converted <= 0) {
    // fprintf(stderr, "MultiByteToWideChar failed with error %lu\n", GetLastError());
    free(wide_str);
    return NULL;
  }
  return wide_str;
}

// Converts a WCHAR (UTF-16) string to a newly allocated UTF-8 string.
// Returns NULL on failure. Caller must free the returned pointer.
static char *wide_to_utf8(const WCHAR *wide_str) {
  if (!wide_str)
    return NULL;
  int bytes_needed = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, NULL, 0, NULL, NULL);
  if (bytes_needed <= 0) {
    // fprintf(stderr, "WideCharToMultiByte (pre-flight) failed with error %lu\n", GetLastError());
    return NULL;
  }
  char *utf8_str = (char *)malloc(bytes_needed); // Bytes includes null terminator
  if (!utf8_str) {
    // fprintf(stderr, "Failed to allocate memory for utf8 string\n");
    return NULL;
  }
  int bytes_converted = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_str, bytes_needed, NULL, NULL);
  if (bytes_converted <= 0) {
    // fprintf(stderr, "WideCharToMultiByte failed with error %lu\n", GetLastError());
    free(utf8_str);
    return NULL;
  }
  return utf8_str;
}

// Converts Windows FILETIME to POSIX time_t.
// See https://stackoverflow.com/questions/6161776/convert-windows-filetime-to-second-in-unix-linux
static void filetime_to_time_t(const FILETIME *ft, time_t *t) {
  ULARGE_INTEGER uli;
  uli.LowPart = ft->dwLowDateTime;
  uli.HighPart = ft->dwHighDateTime;
  // Windows FILETIME is 100-nanosecond intervals since January 1, 1601.
  // time_t is seconds since January 1, 1970.
  // Difference is 11644473600 seconds.
  const ULONGLONG epoch_diff = 116444736000000000ULL;
  if (uli.QuadPart < epoch_diff) {
    *t = 0; // Time is before the Unix epoch
  } else {
    *t = (time_t)((uli.QuadPart - epoch_diff) / 10000000ULL);
  }
}

// Populates a struct stat (partially) from WIN32_FIND_DATAW.
// This is a best-effort mapping as not all fields have direct equivalents.
static void populate_stat_from_find_data(struct stat *s, const WIN32_FIND_DATAW *findData) {
  memset(s, 0, sizeof(struct stat)); // Initialize all fields to 0

  // File Type and Mode (Basic)
  if (findData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    s->st_mode |= S_IFDIR | 0755; // Directory with typical permissions
  } else {
    s->st_mode |= S_IFREG | 0644; // Regular file with typical permissions
  }
  if (findData->dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
    s->st_mode &= ~0222; // Remove write permissions
  }

  // File Size
  ULARGE_INTEGER fileSize;
  fileSize.LowPart = findData->nFileSizeLow;
  fileSize.HighPart = findData->nFileSizeHigh;
// Be careful about potential truncation if using a 32-bit build and large files
// Using LONGLONG cast for intermediate calculation might help, but st_size might still be 32-bit
#ifdef _WIN64
  s->st_size = fileSize.QuadPart;
#else
  // Potential truncation for files > 4GB on 32-bit builds where st_size is 32-bit
  if (fileSize.QuadPart > 0xFFFFFFFF) {
    s->st_size = 0xFFFFFFFF; // Max value for 32-bit unsigned? Or -1? Check definition.
                             // Consider logging a warning here
  } else {
    s->st_size = fileSize.LowPart; // Assuming st_size is 32-bit here
  }
  // A better approach for 32-bit might involve using _stati64 and struct __stat64
  // but that changes the struct zsv_foreach_dirent_handle definition slightly.
  // Sticking to standard 'struct stat' as requested for now.
#endif

  // Timestamps
  filetime_to_time_t(&findData->ftCreationTime, &s->st_ctime);
  filetime_to_time_t(&findData->ftLastAccessTime, &s->st_atime);
  filetime_to_time_t(&findData->ftLastWriteTime, &s->st_mtime);

  // Other fields (st_dev, st_ino, st_nlink, st_uid, st_gid)
  // These don't have direct, easily accessible equivalents in WIN32_FIND_DATAW.
  // Setting them to 0 or default values.
  s->st_nlink = 1; // Typically 1 for files on Windows, can be >1 for dirs but hard to get
                   // s->st_dev = 0; // No easy equivalent
                   // s->st_ino = 0; // No easy equivalent (File ID requires GetFileInformationByHandle)
                   // s->st_uid = 0; // No concept of Unix UID/GID
                   // s->st_gid = 0;
}
