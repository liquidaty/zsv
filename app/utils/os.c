/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

// must precede every include: wasi-libc hides both arc4random_uniform() and
// getentropy(), used by zsv_mkstemp() below, behind _GNU_SOURCE/_BSD_SOURCE.
// app/Makefile passes -D_GNU_SOURCE today, but relying on that leaves the file
// uncompilable on its own; declare the dependency here, as check.c/echo.c/
// overwrite.c/serialize.c already do.
#define _GNU_SOURCE 1

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

#include <stdlib.h> // mkstemp

#ifndef __wasi__
int zsv_mkstemp(char *tmpl) {
  return mkstemp(tmpl);
}
#else
#include <fcntl.h>
#include <stdint.h> // uint32_t
#include <string.h>
#include <sys/stat.h> // S_IRUSR S_IWUSR

#define ZSV_MKSTEMP_MIN_X 6
#define ZSV_MKSTEMP_TRIES 100

static const char zsv_mkstemp_charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
#define ZSV_MKSTEMP_CHARSET_N (sizeof(zsv_mkstemp_charset) - 1)

/**
 * Fill s[0, n) with uniformly-drawn characters from zsv_mkstemp_charset.
 * Both sources are CSPRNGs backed by __wasi_random_get; arc4random_uniform() is
 * preferred but is only present when configure detected it, so getentropy() --
 * which wasi-libc always provides -- is the fallback. Both need _GNU_SOURCE,
 * defined at the top of this file.
 * Returns 0, or -1 with errno set.
 */
static int zsv_mkstemp_fill(char *s, size_t n) {
#ifdef HAVE_ARC4RANDOM_UNIFORM
  for (size_t i = 0; i < n; i++)
    s[i] = zsv_mkstemp_charset[arc4random_uniform((uint32_t)ZSV_MKSTEMP_CHARSET_N)];
  return 0;
#else
  // reject the tail that would bias the modulo (256 % 36 == 4)
  const unsigned limit = 256 - (256 % ZSV_MKSTEMP_CHARSET_N);
  unsigned char buf[64]; // getentropy() caps a single request at 256 bytes
  // bounded so a pathological entropy source cannot spin forever; each draw
  // keeps a byte with p = 252/256, so exhausting this is not reachable in practice
  for (unsigned rounds = 0; rounds < ZSV_MKSTEMP_TRIES; rounds++) {
    size_t filled = 0;
    while (filled < n) {
      size_t want = n - filled < sizeof(buf) ? n - filled : sizeof(buf);
      if (getentropy(buf, want))
        return -1;
      size_t before = filled;
      for (size_t i = 0; i < want && filled < n; i++)
        if (buf[i] < limit)
          s[filled++] = zsv_mkstemp_charset[buf[i] % ZSV_MKSTEMP_CHARSET_N];
      if (filled == before) // no byte survived rejection; count it and retry
        break;
    }
    if (filled == n)
      return 0;
  }
  errno = EIO;
  return -1;
#endif
}

int zsv_mkstemp(char *tmpl) {
  size_t len = strlen(tmpl);
  size_t x_count = 0;
  while (x_count < len && tmpl[len - x_count - 1] == 'X')
    x_count++;
  if (x_count < ZSV_MKSTEMP_MIN_X) {
    errno = EINVAL;
    return -1;
  }
  // O_EXCL is what makes this safe; the CSPRNG only keeps collisions rare
  for (unsigned tries = 0; tries < ZSV_MKSTEMP_TRIES; tries++) {
    if (zsv_mkstemp_fill(tmpl + len - x_count, x_count))
      return -1;
    int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0 || errno != EEXIST)
      return fd;
  }
  errno = EEXIST;
  return -1;
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

unsigned int zsv_get_number_of_cores(void) {
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
