#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy

#include "io.h"

static char *slashes_to_backslashes_if_needed(const char *path, DWORD *rc) {
  char *tmp = NULL;
  if (strchr(path, '/')) {
    tmp = strdup(path);
    if (!tmp) {
      perror(path);
      *rc = ERROR_OUTOFMEMORY;
      return NULL;
    }
    for (size_t i = 0, j = strlen(path); i < j; i++)
      if (tmp[i] == '/')
        tmp[i] = '\\';
  }
  *rc = 0;
  return tmp;
}

// Convert slashes to backslashes and then
// make sure we have "\\\\?\\" or "\\\\?\\UNC\\" prefix if/as necessary or requested
// Note: We assume the input 'original_path' is UTF-8 encoded.
// The returned path, if not NULL, will also be UTF-8 encoded.
// Sets *err to 0, or to the Windows error code when a NULL return is a failure
// (ERROR_NO_UNICODE_TRANSLATION if original_path is not valid UTF-8)
static char *zsv_long_path_prefix(const char *original_path, unsigned char always_prefix, DWORD *err) {
  *err = 0;
  if (original_path == NULL || original_path[0] == '\0') {
    return NULL; // Handle NULL or empty input
  }

  wchar_t *wide_original_path = NULL;
  wchar_t *wide_full_path = NULL;
  wchar_t *prefixed_wide_path = NULL;
  char *result_path_utf8 = NULL;
  DWORD last_error = 0;

  // --- 1. Convert input UTF-8 path to wide char (UTF-16) ---
  int original_len_bytes = strlen(original_path);
  int wide_len_needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, original_path, original_len_bytes, NULL, 0);
  if (wide_len_needed == 0) {
    last_error = GetLastError();
    if (last_error != ERROR_NO_UNICODE_TRANSLATION) // a path that is not UTF-8 is the caller's to report
      fprintf(stderr, "zsv_ensureLongPathPrefix: MultiByteToWideChar (size check) failed: %lu\n", last_error);
    goto cleanup;
  }

  wide_original_path = (wchar_t *)malloc((wide_len_needed + 1) * sizeof(wchar_t));
  if (!wide_original_path) {
    last_error = ERROR_OUTOFMEMORY;
    perror("zsv_ensureLongPathPrefix: malloc for wide_original_path failed");
    goto cleanup;
  }
  int converted_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, original_path, original_len_bytes,
                                            wide_original_path, wide_len_needed);
  if (converted_chars == 0) {
    last_error = GetLastError();
    fprintf(stderr, "zsv_ensureLongPathPrefix: MultiByteToWideChar failed: %lu\n", last_error);
    goto cleanup;
  }
  wide_original_path[wide_len_needed] = L'\0'; // Null-terminate

  // --- 2. Get the full absolute path using GetFullPathNameW ---
  DWORD full_path_len_needed_wchars = GetFullPathNameW(wide_original_path, 0, NULL, NULL);
  if (full_path_len_needed_wchars == 0) {
    last_error = GetLastError();
    fprintf(stderr, "zsv_ensureLongPathPrefix: GetFullPathNameW (size check) failed for '%ls': %lu\n",
            wide_original_path, last_error);
    goto cleanup;
  }

  wide_full_path = (wchar_t *)malloc(full_path_len_needed_wchars * sizeof(wchar_t));
  if (!wide_full_path) {
    last_error = ERROR_OUTOFMEMORY;
    perror("zsv_ensureLongPathPrefix: malloc for wide_full_path failed");
    goto cleanup;
  }

  DWORD full_path_len_copied_wchars =
    GetFullPathNameW(wide_original_path, full_path_len_needed_wchars, wide_full_path, NULL);
  if (full_path_len_copied_wchars == 0 || full_path_len_copied_wchars >= full_path_len_needed_wchars) {
    last_error = full_path_len_copied_wchars ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
    // Error or buffer too small (shouldn't happen with correct size check)
    fprintf(stderr, "zsv_ensureLongPathPrefix: GetFullPathNameW failed or buffer issue for '%ls': %lu\n",
            wide_original_path, last_error);
    goto cleanup;
  }
  // wide_full_path now contains the absolute path, null-terminated, with backslashes.

  // Original wide path no longer needed
  free(wide_original_path);
  wide_original_path = NULL;

  // --- 3. Check length of the ABSOLUTE path and always_prefix flag ---
  size_t absolute_path_wlen = wcslen(wide_full_path); // Use wcslen for clarity

  if (!ZSV_WIN_PATH_LEN_IS_LONG(absolute_path_wlen) && !always_prefix) {
    // Path is short, and we don't always prefix.
    // Return NULL as per the original function's contract for non-prefixing cases.
    goto cleanup; // result_path_utf8 is already NULL
  }

  // --- Path is long (>= MAX_PATH) or always_prefix is set ---

  // --- Define prefixes (wide char) ---
  const wchar_t *prefix_std = L"\\\\?\\";
  const wchar_t *prefix_unc = L"\\\\?\\UNC\\";
  const size_t prefix_std_len = 4; // wcslen(prefix_std)
  const size_t prefix_unc_len = 8; // wcslen(prefix_unc)
  const wchar_t *prefix_dev = L"\\\\.\\";
  const size_t prefix_dev_len = 4; // wcslen(prefix_dev)

  // --- 4. Check if the absolute path is already correctly prefixed ---
  // GetFullPathNameW gives a device name such as NUL as \\.\NUL. A device-namespace
  // path is not a UNC path and would not resolve if rewritten as \\?\UNC\.\NUL
  if (wcsncmp(wide_full_path, prefix_std, prefix_std_len) == 0 ||
      wcsncmp(wide_full_path, prefix_unc, prefix_unc_len) == 0 ||
      wcsncmp(wide_full_path, prefix_dev, prefix_dev_len) == 0) {
    // Already has a standard, UNC or device prefix.
    // We still need to return it (converted back to UTF-8) because
    // prefixing *was* deemed necessary (long path or always_prefix=true).
    prefixed_wide_path = wide_full_path; // Use the existing full path
    wide_full_path = NULL;               // Prevent double free
  } else {
    // --- Needs prefixing ---

    // Determine if it's a UNC path (starts with \\ but not \\?)
    // GetFullPathNameW should return paths like \\server\share...
    BOOL is_unc = (absolute_path_wlen >= 2 && wide_full_path[0] == L'\\' && wide_full_path[1] == L'\\');

    size_t new_wide_size_wchars = 0;

    if (is_unc) {
      // UNC Path: Needs \\?\UNC\ prefix
      // Path part starts after the leading '\\'
      const wchar_t *path_part = wide_full_path + 2;
      size_t path_part_len = wcslen(path_part);
      // New size = prefix len + path part len + 1 (null)
      new_wide_size_wchars = prefix_unc_len + path_part_len + 1;
      prefixed_wide_path = (wchar_t *)malloc(new_wide_size_wchars * sizeof(wchar_t));
      if (!prefixed_wide_path) {
        last_error = ERROR_OUTOFMEMORY;
        perror("zsv_ensureLongPathPrefix (UNC): malloc failed");
        goto cleanup;
      }
      // Construct: wcscpy + wcscat is safe here due to exact size calculation
      wcscpy(prefixed_wide_path, prefix_unc);
      wcscat(prefixed_wide_path, path_part); // Append server\share...
    } else {
      // Standard Path: Needs \\?\ prefix
      // New size = prefix len + original absolute path len + 1 (null)
      new_wide_size_wchars = prefix_std_len + absolute_path_wlen + 1;
      prefixed_wide_path = (wchar_t *)malloc(new_wide_size_wchars * sizeof(wchar_t));
      if (!prefixed_wide_path) {
        last_error = ERROR_OUTOFMEMORY;
        perror("zsv_ensureLongPathPrefix (STD): malloc failed");
        goto cleanup;
      }
      // Construct: wcscpy + wcscat is safe here
      wcscpy(prefixed_wide_path, prefix_std);
      wcscat(prefixed_wide_path, wide_full_path);
    }
  }

  // --- 5. Convert the (potentially) prefixed wide path back to UTF-8 ---
  if (prefixed_wide_path) { // Should always be true if we reach here
                            // Pass -1 for null termination handling
    int utf8_len_needed = WideCharToMultiByte(CP_UTF8, 0, prefixed_wide_path, -1, NULL, 0, NULL, NULL);
    if (utf8_len_needed == 0) {
      last_error = GetLastError();
      fprintf(stderr, "zsv_ensureLongPathPrefix: WideCharToMultiByte (size check) failed: %lu\n", last_error);
      goto cleanup;
    }

    result_path_utf8 = (char *)malloc(utf8_len_needed); // Includes null terminator space
    if (!result_path_utf8) {
      last_error = ERROR_OUTOFMEMORY;
      perror("zsv_ensureLongPathPrefix: malloc for final result_path_utf8 failed");
      goto cleanup;
    }

    int bytes_written =
      WideCharToMultiByte(CP_UTF8, 0, prefixed_wide_path, -1, result_path_utf8, utf8_len_needed, NULL, NULL);
    if (bytes_written == 0) {
      last_error = GetLastError();
      fprintf(stderr, "zsv_ensureLongPathPrefix: WideCharToMultiByte failed: %lu\n", last_error);
      // Free the allocated result buffer before cleaning up others
      free(result_path_utf8);
      result_path_utf8 = NULL;
      goto cleanup;
    }
  }

cleanup:
  // Free intermediate allocations
  free(wide_original_path); // Safe to free NULL
  free(wide_full_path);     // Safe to free NULL (will be NULL if assigned to prefixed_wide_path or on early exit)
  free(prefixed_wide_path); // Safe to free NULL (will be NULL if no prefixing happened or on early exit)

  // Return the final UTF-8 path (which is NULL if no prefixing was needed or if an error occurred)
  *err = last_error;
  return result_path_utf8;
}

char *zsv_ensureLongPathPrefix(const char *original_path, unsigned char always_prefix) {
  DWORD err;
  return zsv_long_path_prefix(original_path, always_prefix, &err);
}

int zsv_win_path_is_long(const char *path) {
  if (strlen(path) >= MAX_PATH)
    return 1;
  // fewer than MAX_PATH bytes converts to fewer than MAX_PATH UTF-16 units. The
  // ANSI code page is how fopen() reads the path (UTF-8 under zsv's manifest)
  wchar_t wpath[MAX_PATH];
  if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, wpath, MAX_PATH))
    return 0;
  // the exact length when the absolute path fits in MAX_PATH units with its NUL,
  // else the size it needs, which exceeds MAX_PATH; 0 on failure
  wchar_t full[MAX_PATH];
  DWORD len = GetFullPathNameW(wpath, MAX_PATH, full, NULL);
  return ZSV_WIN_PATH_LEN_IS_LONG(len);
}

DWORD zsv_pathToPrefixedWidePath(const char *path_utf8, wchar_t **result) {
  // --- Input Validation ---
  if (!path_utf8 || !result) {
    return ERROR_INVALID_PARAMETER;
  }
  *result = NULL; // Initialize result pointer

  // --- Intermediate Variables ---
  char *prefixed_utf8 = NULL;         // Result from zsv_ensureLongPathPrefix
  const char *utf8_to_convert = NULL; // Points to the UTF-8 string to be converted
  wchar_t *final_wide_path = NULL;    // Final result path pointer (was intermediate)
  DWORD error_code = 0;               // Holds error codes

  // --- 2. Add prefix
  // A NULL return with no error means path_utf8 is empty; with an error, converting the
  // unprefixed path would act on a different file than the caller named or fail obscurely
  prefixed_utf8 = zsv_long_path_prefix(path_utf8, 1, &error_code);
  if (error_code)
    goto cleanup;
  // If prefixed_utf8 is not NULL, it's newly allocated (with correct prefix).

  // Determine the final UTF-8 string to convert to wide char
  utf8_to_convert = prefixed_utf8 ? prefixed_utf8 : path_utf8;

  // --- 3. Convert Selected UTF-8 Path to Wide Char (UTF-16) ---
  // Calculate required buffer size (including null terminator)
  int wideCharLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_to_convert, -1, NULL, 0);
  if (wideCharLen == 0) {
    error_code = GetLastError();
    fprintf(stderr, "Error calculating wide char length for '%s': %lu\n", utf8_to_convert, error_code);
    goto cleanup;
  }

  // Allocate buffer
  final_wide_path = (wchar_t *)calloc(wideCharLen + 1, sizeof(wchar_t));
  if (!final_wide_path) {
    perror("Error allocating memory for final wide path");
    error_code = ERROR_OUTOFMEMORY;
    goto cleanup;
  }

  // Perform conversion
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_to_convert, -1, final_wide_path, wideCharLen) == 0) {
    error_code = GetLastError();
    fprintf(stderr, "Error converting path '%s' to wide char: %lu\n", utf8_to_convert, error_code);
    // final_wide_path will be freed in cleanup
    goto cleanup;
  }
  // final_wide_path now holds the result, correctly prefixed if needed.

  // --- 4. Set Result and Return Success ---
  // The intermediate wide path is now the final path.
  *result = final_wide_path;
  final_wide_path = NULL; // Prevent freeing in cleanup as ownership is transferred
  error_code = 0;         // Success

cleanup:
  // Free all intermediate allocations. free(NULL) is safe.
  free(prefixed_utf8);
  free(final_wide_path); // Free only if not transferred to *result

  // If an error occurred before setting *result, ensure it's NULL
  if (error_code != 0) {
    *result = NULL;
  }

  return error_code;
}
