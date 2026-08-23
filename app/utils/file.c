/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

// must precede every include: asprintf() below is hidden behind _GNU_SOURCE.
// app/Makefile passes -D_GNU_SOURCE today, but relying on that leaves the file
// uncompilable on its own; see the same note in os.c
#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h> // for close()
#include <fcntl.h>  // open

#include <zsv/utils/dirs.h>
#include <zsv/utils/os.h>
#include <zsv/utils/file.h>

/**
 * Get a temp file name. The returned value, if any, will have been allocated
 * on the heap, and the caller should `free()`
 *
 * @param prefix string with which the resulting file name will be prefixed
 */
#if defined(_WIN32) || defined(WIN32) || defined(WIN)
#include <windows.h>

char *zsv_get_temp_filename(const char *prefix) {
  if (prefix && strlen(prefix) > 3) {
    fprintf(stderr, "Warning: zsv_get_temp_filename called with prefix longer than 3 chars (%s)\n", prefix);
  }
  TCHAR lpTempPathBuffer[MAX_PATH];
  DWORD dwRetVal = GetTempPath(MAX_PATH,          // length of the buffer
                               lpTempPathBuffer); // buffer for path
  if (!(dwRetVal > 0 && dwRetVal < MAX_PATH))
    zsv_perror("GetTempPath");
  else {
    char szTempFileName[MAX_PATH];
    UINT uRetVal = GetTempFileName(lpTempPathBuffer, // directory for tmp files
                                   TEXT(prefix),     // temp file name prefix
                                   0,                // create unique name
                                   szTempFileName);  // buffer for name
    if (uRetVal > 0)
      return strdup(szTempFileName);
    zsv_perror(lpTempPathBuffer);
  }

  return NULL;
}
#else

char *zsv_get_temp_filename(const char *prefix) {
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir && !*tmpdir) // TMPDIR= is not a directory; treat it as unset
    tmpdir = NULL;
  if (!prefix)
    prefix = "";
  // Try $TMPDIR, then the current directory. The fallback matters on wasi,
  // where an inherited TMPDIR is an absolute host path that is almost never
  // among the preopened directories, so every temp file would otherwise fail;
  // "." is already this function's behavior when TMPDIR is unset.
  for (unsigned i = 0; i < 2; i++) {
    const char *dir = (i == 0 && tmpdir) ? tmpdir : ".";
    char *s = NULL;
    asprintf(&s, "%s%c%sXXXXXXXX", dir, FILESLASH, prefix);
    if (!s) {
      // asprintf() is not guaranteed to set errno, so don't report a stale one;
      // allocation failure is the only way it fails
      fprintf(stderr, "%s%c%s: out of memory\n", dir, FILESLASH, prefix);
      return NULL;
    }
    int fd = zsv_mkstemp(s);
    if (fd >= 0) { // 0 is a legal descriptor
      close(fd);
      return s;
    }
    int e = errno; // free() may clobber it before we return
    free(s);
    errno = e;
    if (!tmpdir) // the fallback is what we just tried
      break;
    // not silent: the retry can put a large spill file somewhere unexpected.
    // Warned per occurrence rather than once -- a plain `static` flag would be a
    // data race, since parallel select reaches this from its worker threads.
    if (i == 0)
      fprintf(stderr, "Warning: $TMPDIR (%s) is not usable; trying the current directory\n", tmpdir);
  }
  return NULL;
}

#endif

/**
 * Get a temp file name for a caller that creates the file itself with
 * `O_CREAT|O_EXCL`; see zsv/utils/file.h
 */
char *zsv_get_temp_filename_excl(const char *prefix) {
  // truncate the prefix to 3 chars: the Windows implementation's
  // GetTempFileName() ignores anything longer (and warns); truncating keeps
  // the resulting names consistent across platforms
  char pfx[4];
  size_t n = prefix ? strlen(prefix) : 0;
  if (n > sizeof(pfx) - 1)
    n = sizeof(pfx) - 1;
  if (n)
    memcpy(pfx, prefix, n);
  pfx[n] = '\0';

  char *fn = zsv_get_temp_filename(pfx);
  if (fn && zsv_remove(fn)) {
    int e = errno;
    free(fn);
    errno = e;
    return NULL;
  }
  return fn;
}

/**
 *  Replacement for tmpfile().
 *  Returns filename; file must be manually removed after fclose
 *
 *  @param mode optional mode passed to fopen(); if NULL, defaults to "wb"
 */
FILE *zsv_tmpfile(const char *prefix, char **filename, const char *mode) {
  char *fn = zsv_get_temp_filename(prefix);
  if (fn) {
    FILE *f = fopen(fn, mode == NULL ? "wb" : mode);
    if (f) {
      *filename = fn;
      return f;
    }
  }
  int e = errno;
  if (fn)
    zsv_remove(fn); // zsv_get_temp_filename() created it; do not orphan it
  free(fn);
  *filename = NULL; // set with the return value on every path
  errno = e;
  return NULL;
}

/**
 * Temporarily redirect a FILE * (e.g. stdout / stderr) to a temp file
 * temp_filename and bak are set as return values
 * caller must free temp_filename
 *
 * On wasm this is unsupported: wasi has no file descriptor duplication
 * (no dup()/dup2())
 *
 * @param old_fd file descriptor of file to dupe e.g. fileno(stdout);
 * @return fd needed to pass on to zsv_redirect_file_from_temp, or -1 on error
 */
#if defined(_WIN32) || defined(__FreeBSD__)
#include <sys/stat.h> // S_IRUSR S_IWUSR
#endif

int zsv_redirect_file_to_temp(FILE *f, const char *tempfile_prefix, char **temp_filename) {
  *temp_filename = NULL;
#ifdef __wasi__
  (void)f;
  (void)tempfile_prefix;
  errno = ENOTSUP;
  return -1;
#else
  int e;
  int new_fd = -1;
  int old_fd = fileno(f);
  if (old_fd < 0)
    return -1;
  fflush(f);
  int bak = dup(old_fd);
  if (bak < 0)
    return -1;

  // zsv_get_temp_filename() already created the file, so reopen without O_EXCL
  if (!(*temp_filename = zsv_get_temp_filename(tempfile_prefix)) ||
      (new_fd = open(*temp_filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0 || dup2(new_fd, old_fd) < 0)
    goto cleanup;

  close(new_fd);
  return bak;

cleanup:
  e = errno;
  if (new_fd >= 0)
    close(new_fd);
  if (*temp_filename) {
    zsv_remove(*temp_filename); // created by zsv_get_temp_filename(); nobody else can remove it now
    free(*temp_filename);
    *temp_filename = NULL;
  }
  close(bak);
  errno = e;
  return -1;
#endif
}

/**
 * Restore a FILE * that was redirected by zsv_redirect_file_to_temp()
 */
void zsv_redirect_file_from_temp(FILE *f, int bak, int old_fd) {
#ifdef __wasi__
  (void)f;
  (void)bak;
  (void)old_fd;
#else
  if (bak < 0) // zsv_redirect_file_to_temp() failed; nothing was redirected
    return;
  fflush(f);
  dup2(bak, old_fd);
  close(bak);
#endif
}

#if defined(_WIN32) || defined(WIN32) || defined(WIN)
int zsv_file_exists(const char *filename) {
  DWORD attributes = GetFileAttributes(filename);
  return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}
#else
#include <sys/stat.h> // S_IRUSR S_IWUSR

int zsv_file_exists(const char *filename) {
  struct stat buffer;
  if (stat(filename, &buffer) == 0) {
    char is_dir = buffer.st_mode & S_IFDIR ? 1 : 0;
    if (!is_dir)
      return 1;
  }
  return 0;
}
#endif

#include <sys/stat.h>
#ifndef S_ISREG // MSVC CRT lacks the POSIX macro
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
int zsv_file_is_regular(FILE *f) {
  struct stat st;
  return f && fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * Copy a file, given source and destination paths
 * On error, output error message and return non-zero
 */
int zsv_copy_file(const char *src, const char *dest) {
  // create one or more directories if needed
  if (zsv_mkdirs(dest, 1)) {
    fprintf(stderr, "Unable to create directories needed for %s\n", dest);
    return -1;
  }

  // copy the file
  int err = 0;
  FILE *fsrc = zsv_fopen(src, "rb");
  if (!fsrc)
    err = errno ? errno : -1, perror(src);
  else {
    FILE *fdest = zsv_fopen(dest, "wb");
    if (!fdest)
      err = errno ? errno : -1, perror(dest);
    else {
      err = zsv_copy_file_ptr(fsrc, fdest);
      if (err)
        perror(dest);
      fclose(fdest);
    }
    fclose(fsrc);
  }
  return err;
}

/**
 * Copy a file-like, given source and destination handles
 * and read/write functions
 * Return error number per errno.h
 */
int zsv_copy_filelike_ptr(
  FILE *src, size_t (*freadx)(void *restrict ptr, size_t size, size_t nitems, void *restrict stream), FILE *dest,
  size_t (*fwritex)(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream)) {
  int err = 0;
  char buffer[4096 * 16];
  size_t bytes_read;
  while ((bytes_read = freadx(buffer, 1, sizeof(buffer), src)) > 0) {
    if (fwritex(buffer, 1, bytes_read, dest) != bytes_read) {
      err = errno ? errno : -1;
      break;
    }
  }
  return err;
}

/**
 * Copy a file, given source and destination FILE pointers
 * Return error number per errno.h
 */
int zsv_copy_file_ptr(FILE *src, FILE *dest) {
  return zsv_copy_filelike_ptr(
    src, (size_t(*)(void *restrict ptr, size_t size, size_t nitems, void *restrict stream))fread, dest,
    (size_t(*)(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream))fwrite);
}

size_t zsv_dir_len_basename(const char *filepath, const char **basename) {
  for (size_t len = strlen(filepath); len; len--) {
    if (filepath[len - 1] == '/' || filepath[len - 1] == '\\') {
      *basename = filepath + len;
      return len - 1;
    }
  }

  *basename = filepath;
  return 0;
}

int zsv_file_readable(const char *filename, int *err, FILE **f_out) {
  FILE *f;
  int rc;
  if (err)
    *err = 0;
  // to do: use fstat()
  if ((f = zsv_fopen(filename, "rb")) == NULL) {
    rc = 0;
    if (err)
      *err = errno;
    else
      perror(filename);
  } else {
    rc = 1;
    if (f_out)
      *f_out = f;
    else
      fclose(f);
  }
  return rc;
}

/**
 * Function that is the same as `fwrite()`, but can be used as a callback
 * argument to `zsv_set_scan_filter()`
 */
size_t zsv_filter_write(void *FILEp, unsigned char *buff, size_t bytes_read) {
  fwrite(buff, 1, bytes_read, (FILE *)FILEp);
  return bytes_read;
}

int zsv_no_printf(void *_ctx, const char *_format, ...) {
  // do nothing!
  (void)_ctx;
  (void)_format;
  return 0;
}

#include "file-mem.c"
