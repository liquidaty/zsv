#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy

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

char *zsv_ensureLongPathPrefix(const char *original_path, unsigned char always_prefix) {
  // --- Basic input validation ---
  if (original_path == NULL) {
    return NULL;
  }

  // --- Define prefixes ---
  const char *prefix_std = "\\\\?\\";      // For standard paths C:\...
  const char *prefix_unc = "\\\\?\\UNC\\"; // For UNC paths \\server\share...
  const size_t prefix_std_len = 4;         // strlen(prefix_std)
  const size_t prefix_unc_len = 8;         // strlen(prefix_unc)

  // --- Check length ---
  size_t original_len = strlen(original_path);

  if (original_len < MAX_PATH && always_prefix == 0) {
    // Path is short, no prefix needed based on length.
    return NULL;
  }

  // --- Path is long (>= MAX_PATH) ---

  // --- Check if already correctly prefixed ---
  if (strncmp(original_path, prefix_std, prefix_std_len) == 0 ||
      strncmp(original_path, prefix_unc, prefix_unc_len) == 0) {
    // Already has a standard or UNC prefix. Nothing to do.
    return NULL;
  }

  // --- Determine if it's a UNC path ---
  // Check if it starts with exactly two backslashes
  // (Handles \\server but avoids incorrectly identifying \\?\...)
  BOOL is_unc = (original_len >= 2 && original_path[0] == '\\' && original_path[1] == '\\' &&
                 original_path[2] != '?'); // Avoid matching \\?\ incorrectly

  // --- Allocate memory and construct new prefixed path ---
  char *prefixed_path = NULL;
  size_t new_size = 0;
  int written = -1;

  if (is_unc) {
    // UNC Path: Needs \\?\UNC\ prefix
    // Size = prefix len + path len (skipping one '\') + null terminator
    // We skip the first '\' because "\\?\UNC\" effectively replaces "\\"
    const char *path_part = original_path + 1; // Point after first '\'
    new_size = prefix_unc_len + strlen(path_part) + 1;
    prefixed_path = (char *)malloc(new_size);
    if (!prefixed_path) {
      perror("zsv_ensureLongPathPrefix (UNC): malloc failed");
      return NULL;
    }
    written = snprintf(prefixed_path, new_size, "%s%s", prefix_unc, path_part);
  } else {
    // Standard Path: Needs \\?\ prefix
    // Size = prefix len + original path len + null terminator
    new_size = prefix_std_len + original_len + 1;
    prefixed_path = (char *)malloc(new_size);
    if (!prefixed_path) {
      perror("zsv_ensureLongPathPrefix (STD): malloc failed");
      return NULL;
    }
    written = snprintf(prefixed_path, new_size, "%s%s", prefix_std, original_path);
  }

  // Check snprintf result
  if (written < 0 || (size_t)written >= new_size) {
    fprintf(stderr, "zsv_ensureLongPathPrefix: snprintf error or truncation\n");
    free(prefixed_path);
    return NULL;
  }

  return prefixed_path; // Return newly allocated string
}

DWORD zsv_pathToPrefixedWidePath(const char *path_utf8, wchar_t **result) {
  // --- Input Validation ---
  if (!path_utf8 || !result) {
    return ERROR_INVALID_PARAMETER;
  }
  *result = NULL; // Initialize result pointer

  // --- Intermediate Variables ---
  char *tmp_utf8_slashes = NULL;      // Result from slash conversion
  char *prefixed_utf8 = NULL;         // Result from zsv_ensureLongPathPrefix
  const char *utf8_to_convert = NULL; // Points to the UTF-8 string to be converted
  wchar_t *final_wide_path = NULL;    // Final result path pointer (was intermediate)
  DWORD error_code = 0;               // Holds error codes

  // --- 1. Convert Slashes to Backslashes (if needed) ---
  DWORD rc_slash = 0;
  tmp_utf8_slashes = slashes_to_backslashes_if_needed(path_utf8, &rc_slash);
  if (rc_slash) {
    fprintf(stderr, "Error converting slashes: %lu\n", rc_slash);
    error_code = rc_slash;
    goto cleanup;
  }
  const char *path_after_slashes = tmp_utf8_slashes ? tmp_utf8_slashes : path_utf8;

  // --- 2. Add prefix
  prefixed_utf8 = zsv_ensureLongPathPrefix(path_after_slashes, 1);
  // If prefixed_utf8 is not NULL, it's newly allocated (with correct prefix).
  // If NULL, the path was short or already correctly prefixed.

  // Determine the final UTF-8 string to convert to wide char
  utf8_to_convert = (prefixed_utf8 != NULL) ? prefixed_utf8 : path_after_slashes;

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
  free(tmp_utf8_slashes);
  free(prefixed_utf8);
  free(final_wide_path); // Free only if not transferred to *result

  // If an error occurred before setting *result, ensure it's NULL
  if (error_code != 0) {
    *result = NULL;
  }

  return error_code;
}

/*
DWORD pathToPrefixedWidePath(const char *path_utf8, wchar_t **result) {
  // convert slash to backslash if needed
  DWORD rc;
  char *tmp_utf8 = slashes_to_backslashes_if_needed(path_utf8, &rc);
  if (rc)
    return rc;
  const char *final_path_utf8 = tmp_utf8 ? tmp_utf8 : path_utf8;

  // 2. Convert UTF-8 path to UTF-16 (WCHAR)
  // First, find the required buffer size
  int wideCharLen = MultiByteToWideChar(CP_UTF8, 0, final_path_utf8, -1, NULL, 0);
  if (wideCharLen == 0) {
    DWORD error = GetLastError();
    fprintf(stderr, "Error calculating wide char length: %lu\n", error);
    free(tmp_utf8);
    return error;
  }

  // Allocate buffer for the UTF-16 path
  wchar_t *widePath = (wchar_t *)malloc(wideCharLen * sizeof(wchar_t));
  if (!widePath) {
    perror("Error allocating memory for wide path");
    free(tmp_utf8);
    return ERROR_OUTOFMEMORY;
  }

  // Perform the conversion
  if (MultiByteToWideChar(CP_UTF8, 0, final_path_utf8, -1, widePath, wideCharLen) == 0) {
    DWORD error = GetLastError();
    fprintf(stderr, "Error converting path to wide char: %lu\n", error);
    free(tmp_utf8);
    free(widePath);
    return error;
  }
  free(tmp_utf8);

  // 3. Construct the final path with the appropriate prefix (\\?\ or \\?\UNC\)
  wchar_t *finalPath = NULL;
  size_t finalPathLen = 0;
  const wchar_t *prefix = NULL;

  // Check if the path already has a prefix (less common for input, but good practice)
  if (wcsncmp(widePath, L"\\\\?\\", 4) == 0) {
    // Already has the standard prefix, use as is
    finalPath = widePath; // Use the buffer directly
    widePath = NULL;      // Avoid double free later
  } else if (widePath[0] == L'\\' && widePath[1] == L'\\') {
    // Check for UNC path (e.g., \\server\share)
    // UNC path, needs \\?\UNC\ prefix
    prefix = L"\\\\?\\UNC\\";
    size_t prefixLen = wcslen(prefix);
    // Need to skip the first two '\\' from the original UNC path when appending
    finalPathLen = prefixLen + wcslen(widePath + 1); // +1 to skip one '\' after UNC server/share
    finalPath = (wchar_t *)malloc((finalPathLen + 1) * sizeof(wchar_t));
    if (!finalPath) {
      perror("Error allocating memory for final UNC path");
      free(widePath);
      return ERROR_OUTOFMEMORY;
    }
    // Construct the final path: prefix + rest of UNC path (skipping one leading '\')
    wcscpy(finalPath, prefix);
    // Use widePath + 1 to effectively skip the first '\' (since \\?\UNC\ replaces \\)
    wcscat(finalPath, widePath + 1);
  } else {
    // Assume standard absolute path (e.g., C:\...)
    prefix = L"\\\\?\\";
    size_t prefixLen = wcslen(prefix);
    finalPathLen = prefixLen + wcslen(widePath);
    finalPath = (wchar_t *)malloc((finalPathLen + 1) * sizeof(wchar_t));
    if (!finalPath) {
      perror("Error allocating memory for final path");
      free(widePath);
      return ERROR_OUTOFMEMORY;
    }
    // Construct the final path: prefix + original wide path
    wcscpy(finalPath, prefix);
    wcscat(finalPath, widePath);
  }
  free(widePath);
  *result = finalPath;
  return 0;
}
*/
