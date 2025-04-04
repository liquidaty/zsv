#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy

static char *slashes_to_backslashes_if_needed(const char *path, DWORD *rc) {
  char *tmp = NULL;
  if(strchr(path, '/')) {
    tmp = strdup(path);
    if(!tmp) {
      perror(path);
      *rc = ERROR_OUTOFMEMORY;
      return NULL;
    }
    for(size_t i = 0, j = strlen(path); i < j; i++)
      if(tmp[i] == '/')
        tmp[i] = '\\';
  }
  *rc = 0;
  return tmp;  
}

DWORD pathToPrefixedWidePath(const char* path_utf8, wchar_t **result) {
    // convert slash to backslash if needed
  DWORD rc;
  char *tmp_utf8 = slashes_to_backslashes_if_needed(path_utf8, &rc);
  if(rc)
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
    wchar_t* widePath = (wchar_t*)malloc(wideCharLen * sizeof(wchar_t));
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
    wchar_t* finalPath = NULL;
    size_t finalPathLen = 0;
    const wchar_t* prefix = NULL;

    // Check if the path already has a prefix (less common for input, but good practice)
    if (wcsncmp(widePath, L"\\\\?\\", 4) == 0) {
        // Already has the standard prefix, use as is
        finalPath = widePath; // Use the buffer directly
        widePath = NULL; // Avoid double free later
    } else if (widePath[0] == L'\\' && widePath[1] == L'\\') {
      // Check for UNC path (e.g., \\server\share)
        // UNC path, needs \\?\UNC\ prefix
        prefix = L"\\\\?\\UNC\\";
        size_t prefixLen = wcslen(prefix);
        // Need to skip the first two '\\' from the original UNC path when appending
        finalPathLen = prefixLen + wcslen(widePath + 1); // +1 to skip one '\' after UNC server/share
        finalPath = (wchar_t*)malloc((finalPathLen + 1) * sizeof(wchar_t));
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
        finalPath = (wchar_t*)malloc((finalPathLen + 1) * sizeof(wchar_t));
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
