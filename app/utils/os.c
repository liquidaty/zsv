/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <zsv/utils/os.h>
#include <stdio.h>
#include <errno.h>

#ifdef WIN32
#include "win/fopen_longpath.c"
#include "win/remove_longpath.c"
#endif

/**
 * zsv_fopen(): same as normal fopen(), except on Win it also works with long filenames
 */
#if defined(_WIN32) || defined(WIN32) || defined(WIN)
FILE *zsv_fopen(const char *fname, const char *mode) {
  if (strlen(fname) >= MAX_PATH)
    return zsv_fopen_longpath(fname, mode);
  return fopen(fname, mode);
}
#endif

#ifndef _WIN32

void zsv_perror(const char *s) {
  perror(s);
}

int zsv_replace_file(const char *src, const char *dst) {
  int save_errno = 0;

  if (rename(src, dst) == 0) {
    return 0;
  }

  if (errno != EXDEV) {
    return errno;
  }

  // Fallback: copy and remove
  FILE *fp_in = zsv_fopen(src, "rb");
  if (!fp_in)
    return errno;

  FILE *fp_out = zsv_fopen(dst, "wb");
  if (!fp_out) {
    save_errno = errno;
    fclose(fp_in);
    return save_errno;
  }

  char buffer[4096];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp_in)) > 0) {
    if (fwrite(buffer, 1, bytes_read, fp_out) != bytes_read) {
      fclose(fp_out);
      fclose(fp_in);
      return EOF;
    }
  }

  fclose(fp_out);
  fclose(fp_in);

  if (remove(src) != 0)
    return errno;

  return 0;
}

#else
#include <windows.h>
#include <strsafe.h>

static void strlcpy(register char *dst, register const char *src, size_t n) {
  for (; *src != '\0' && n > 1; n--) {
    *dst++ = *src++;
  }
  *dst = '\0';
}

static void change_slashes_to_backslashes(char *path) {
  int i;
  for (i = 0; path[i] != '\0'; i++) {
    if (path[i] == '/') {
      path[i] = '\\';
    }
    if ((path[i] == '\\') && (i > 0)) {
      while (path[i + 1] == '\\' || path[i + 1] == '/') {
        (void)memmove(path + i + 1, path + i + 2, strlen(path + i + 1));
      }
    }
  }
}

void zsv_win_to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len) {
  char buf[PATH_MAX], buf2[PATH_MAX];
  strlcpy(buf, path, sizeof(buf));

  change_slashes_to_backslashes(buf);

  /* Convert to Unicode and back. If doubly-converted string does not
   * match the original, something is fishy, reject. */
  memset(wbuf, 0, wbuf_len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, (int)wbuf_len);
  WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)wbuf_len, buf2, sizeof(buf2), NULL, NULL);
  if (strcmp(buf, buf2) != 0) {
    wbuf[0] = L'\0';
  }
}

#include <wchar.h>

int zsv_replace_file(const char *src, const char *dest) {
  wchar_t wdest[PATH_MAX], wsrc[PATH_MAX];

  zsv_win_to_unicode(dest, wdest, ARRAY_SIZE(wdest));
  zsv_win_to_unicode(src, wsrc, ARRAY_SIZE(wsrc));

  if (MoveFileExW(wsrc, wdest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) // success
    return 0;

  if (GetLastError() == 2)        // file not found, could be target. use simple rename
    return _wrename(wsrc, wdest); // returns 0 on success

  return 1; // fail
}

static void zsv_win_printLastError(void) {
  DWORD dw = GetLastError();
  LPVOID lpMsgBuf;
  LPVOID lpDisplayBuf;
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
  // Display the error message and exit the process
  lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
  StringCchPrintf((LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR), TEXT("%s\r\n"), lpMsgBuf);
  fprintf(stderr, "%s\r\n", (LPCTSTR)lpDisplayBuf);
  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
}

void zsv_perror(const char *s) {
  if (s && *s)
    fwrite(s, 1, strlen(s), stderr);
  zsv_win_printLastError();
}

#endif

unsigned int zsv_get_number_of_cores() {
  long ncores = 1; // Default to 1 in case of failure

#ifdef _WIN32
  // Implementation for Windows (when cross-compiled with mingw64)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  ncores = sysinfo.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
  // Implementation for Linux and macOS (uses POSIX standard sysconf)
  // _SC_NPROCESSORS_ONLN gets the number of *online* processors.
  ncores = sysconf(_SC_NPROCESSORS_ONLN);
#else
  // Fallback for other POSIX-like systems that might not define the symbol
  // or for unexpected compilation environments.
#error Undefined! _SC_NPROCESSORS_ONLN
  xx ncores = 1;
#endif
  // Ensure we return a positive value
  return (unsigned int)(ncores > 0 ? ncores : 1);
}
