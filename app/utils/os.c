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
#include "win/io.h" // zsv_pathToPrefixedWidePath()
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

static int zsv_replace_file_posix(const char *src, const char *dst) {
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

  // src is removed only once every byte is known to be stored in dst
  char buffer[4096];
  size_t bytes_read;
  errno = 0;
  while (!save_errno && (bytes_read = fread(buffer, 1, sizeof(buffer), fp_in)) > 0)
    if (fwrite(buffer, 1, bytes_read, fp_out) != bytes_read)
      save_errno = errno ? errno : EIO;
  if (!save_errno && ferror(fp_in))
    save_errno = errno ? errno : EIO;
  if (!save_errno && (fflush(fp_out) || zsv_fsync(fileno(fp_out))))
    save_errno = errno ? errno : EIO;
  if (fclose(fp_out) && !save_errno)
    save_errno = errno ? errno : EIO;
  fclose(fp_in);
  if (save_errno)
    return save_errno;

  if (remove(src) != 0)
    return errno;

  return 0;
}

int zsv_replace_file(const char *src, const char *dst) {
  int err = zsv_replace_file_posix(src, dst);
  if (err)
    errno = err; // the cleanup after a failed copy may have changed it
  return err;
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

// the errno value for nonzero Windows error code e: EIO where there is no closer one
static int zsv_win_errno(DWORD e) {
  int err = windows_error_to_errno(e);
  return err ? err : EIO;
}

// *wpath = UTF-8 path as an absolute, \\?\-prefixed wide path of any length, which the
// caller frees. Returns 0, or an errno value (EILSEQ if path is not UTF-8) with *wpath NULL
static int zsv_win_wide_path(const char *path, wchar_t **wpath) {
  DWORD rc = zsv_pathToPrefixedWidePath(path, wpath);
  return rc ? zsv_win_errno(rc) : 0;
}

int zsv_replace_file(const char *src, const char *dest) {
  wchar_t *wsrc = NULL, *wdest = NULL;
  int err = zsv_win_wide_path(src, &wsrc);
  if (!err)
    err = zsv_win_wide_path(dest, &wdest);
  if (!err && !MoveFileExW(wsrc, wdest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DWORD e = GetLastError();
    // the shared mapping gives EACCES; here a file another process holds open is EBUSY
    err = e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION ? EBUSY : zsv_win_errno(e);
  }
  free(wsrc);
  free(wdest);
  if (err)
    errno = err;
  return err;
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

#include <string.h> // strdup

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>

int zsv_fsync(int fd) {
  return fsync(fd);
}

char *zsv_final_path(const char *path) {
  char *resolved = realpath(path, NULL);
  if (!resolved && errno == ENOENT) {
    struct stat st;
    if (!lstat(path, &st)) { // path is a link to nothing
      errno = ENOENT;
      return NULL;
    }
    resolved = strdup(path);
  }
  return resolved;
}

int zsv_same_file(const char *a, const char *b) {
  struct stat sa, sb;
  if (stat(a, &sa) || stat(b, &sb))
    return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
  return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int zsv_copy_permissions(const char *from, const char *to) {
  struct stat st, to_st;
  if (stat(from, &st) || stat(to, &to_st))
    return errno;
  mode_t mode = st.st_mode & 0777;
#ifndef __wasi__ // WASI has no file ownership
  // only root can give the file to from's owner; the group can be kept where the caller belongs to it.
  // Where it cannot, the group bits would apply to another group, so they are capped at the others' bits
  if (to_st.st_gid != st.st_gid && chown(to, (uid_t)-1, st.st_gid))
    mode = (mode & ~(mode_t)070) | (mode & ((mode & 07) << 3));
#endif
  return chmod(to, mode) ? errno : 0;
}

int zsv_create_new_file(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd == -1)
    return errno;
  return close(fd) ? errno : 0;
}
#else
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

int zsv_fsync(int fd) {
  return _commit(fd);
}

char *zsv_final_path(const char *path) {
  return strdup(path);
}

// Windows permission bits are the read-only attribute, which is what _wchmod() sets;
// the Win32 calls take the \\?\-prefixed paths that the CRT's _wstat() may not
int zsv_copy_permissions(const char *from, const char *to) {
  wchar_t *wfrom = NULL, *wto = NULL;
  int err = zsv_win_wide_path(from, &wfrom);
  if (!err)
    err = zsv_win_wide_path(to, &wto);
  if (!err) {
    DWORD from_attrs = GetFileAttributesW(wfrom);
    DWORD to_attrs = from_attrs == INVALID_FILE_ATTRIBUTES ? from_attrs : GetFileAttributesW(wto);
    if (to_attrs == INVALID_FILE_ATTRIBUTES || (((from_attrs ^ to_attrs) & FILE_ATTRIBUTE_READONLY) &&
                                                !SetFileAttributesW(wto, to_attrs ^ FILE_ATTRIBUTE_READONLY)))
      err = zsv_win_errno(GetLastError());
  }
  free(wfrom);
  free(wto);
  return err;
}

int zsv_create_new_file(const char *path) {
  wchar_t *wpath = NULL;
  int err = zsv_win_wide_path(path, &wpath);
  if (!err) {
    HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
      err = zsv_win_errno(GetLastError());
    else if (!CloseHandle(h))
      err = zsv_win_errno(GetLastError());
  }
  free(wpath);
  return err;
}

// fills *out with the volume serial number and file index that identify path's file:
// 1 on success, 0 if the file does not exist, -1 if it cannot be examined
static int zsv_win_file_id(const char *path, BY_HANDLE_FILE_INFORMATION *out) {
  wchar_t *wpath = NULL;
  if (zsv_win_wide_path(path, &wpath))
    return -1;
  HANDLE h = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, NULL);
  free(wpath);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? 0 : -1;
  }
  BOOL ok = GetFileInformationByHandle(h, out);
  CloseHandle(h);
  return ok ? 1 : -1;
}

int zsv_same_file(const char *a, const char *b) {
  BY_HANDLE_FILE_INFORMATION ia, ib;
  int ra = zsv_win_file_id(a, &ia), rb = zsv_win_file_id(b, &ib);
  if (ra < 0 || rb < 0)
    return -1;
  return ra && rb && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh &&
         ia.nFileIndexLow == ib.nFileIndexLow;
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
