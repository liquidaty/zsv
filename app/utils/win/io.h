#ifndef ZSV_WIN_UTILS_IO_H
#define ZSV_WIN_UTILS_IO_H

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy
#include <errno.h>

// remove_longpath.c: the errno value for Windows error code e, or 0 if none fits
int windows_error_to_errno(DWORD windows_error_code);

// the errno value for nonzero Windows error code e: EIO where there is no closer one
static inline int zsv_win_errno(DWORD e) {
  int err = windows_error_to_errno(e);
  return err ? err : EIO;
}

DWORD zsv_pathToPrefixedWidePath(const char *path_utf8, wchar_t **result);

// an absolute path of this many UTF-16 units (excluding the NUL) or more needs the
// \\?\ prefix: without it Win32 calls fail on it unless long paths are enabled
// system-wide
#define ZSV_WIN_PATH_LEN_IS_LONG(wlen) ((wlen) >= MAX_PATH)

/**
 * zsv_win_path_is_long(): 1 if `path` is MAX_PATH bytes or more, or if, made absolute
 * against the current directory, it is too long for a Win32 call without the \\?\
 * prefix; else 0. A relative path shorter than MAX_PATH can still be long. A path
 * that cannot be converted is reported short, so the CRT opens it and reports the
 * error
 */
int zsv_win_path_is_long(const char *path);

#endif
