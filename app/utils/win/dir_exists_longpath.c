#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <stdlib.h>  // For malloc, free
#include <wchar.h>   // Optional, primarily for wcslen if needed
#include <string.h>  // For strncmp, wcslen, wcscpy_s, wcscat_s
#include <shlwapi.h> // For PathIsRelativeA
#include <stdio.h>   // For printf, fprintf in main
// Link against -lShlwapi.for PathIsRelativeA

#include "io.h"

/**
 * Check if a directory exists on Windows, supporting long paths (> MAX_PATH).
 * Handles paths with or without the \\?\ prefix.
 * Works even if directory exists but permissions are limited (as long as attributes can be read).
 *
 * @param path_utf8 The path to check (assumed to be UTF-8 encoded).
 * @return Non-zero (true) if the path exists and is a directory, 0 (false) otherwise.
 */
int zsv_dir_exists_winlp(const char *path_utf8) {
  if (path_utf8 == NULL || path_utf8[0] == '\0') {
    return 0; // Invalid or empty path
  }

  wchar_t *path_to_use;
  DWORD rc = zsv_pathToPrefixedWidePath(path_utf8, &path_to_use);
  if (rc)
    return rc;

  // --- 3. Call GetFileAttributesW ---
  // fprintf(stderr, "Debug: Calling GetFileAttributesW with: %ls\n", path_to_use); // Debug print wide string
  DWORD dwAttrib = GetFileAttributesW(path_to_use);

  // --- 4. Check the result ---
  int result;
  if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
    // Path exists and it is a directory.
    result = 1; // True
  } else {
    // Path does not exist, is not a directory, or access denied to get attributes.
    // GetLastError() could distinguish these cases if needed.
    // fprintf(stderr, "Debug: GetFileAttributesW failed or not a directory. Attributes: 0x%lX, LastError: %lu\n",
    // dwAttrib, GetLastError());
    result = 0; // False
  }

  // --- 5. Cleanup ---
  free(path_to_use);
  return result;
}

#ifdef ZSV_DIR_EXISTS_WINLP_TEST

// --- Main function for testing ---
int main(int argc, char *argv[]) {
  // Check if a path argument was provided
  if (argc != 2) {
    // Note: argv[0] might not be UTF-8 on Windows console, print generic message
    fprintf(stderr, "Usage: <program_name> <path_to_check>\n");
    fprintf(stderr, "Example: %s C:\\Windows\n", argv[0] ? argv[0] : "checkdir");
    fprintf(stderr, "Example: %s \"\\\\?\\C:\\Very Long Path Directory Name\"\n", argv[0] ? argv[0] : "checkdir");
    fprintf(stderr, "Example: %s non_existent_dir\n", argv[0] ? argv[0] : "checkdir");
    return 1; // Indicate error
  }

  // The path provided by the user
  const char *path_to_check = argv[1];

  printf("Checking path: '%s'\n", path_to_check);

  // Call the directory checking function
  int exists = zsv_dir_exists_winlp(path_to_check);

  // Print the result
  if (exists) {
    printf("Result: Directory exists.\n");
  } else {
    printf("Result: Path does not exist or is not a directory.\n");
  }

  return 0; // Indicate success
}
#endif // ZSV_DIR_EXISTS_WINLP_TEST
