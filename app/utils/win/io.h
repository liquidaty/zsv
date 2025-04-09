#ifndef ZSV_WIN_UTILS_IO_H
#define ZSV_WIN_UTILS_IO_H

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>        // For CreateDirectoryW, MultiByteToWideChar, GetLastError etc.
#include <stdio.h>          // For printf, perror
#include <stdlib.h>         // For malloc, free, exit
#include <string.h>         // For strlen, strcpy, strncpy
#include <wchar.h>          // For wide character types and functions like wcslen, wcscpy

DWORD zsv_pathToPrefixedWidePath(const char *path_utf8, wchar_t **result);

#endif
