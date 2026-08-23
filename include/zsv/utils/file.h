/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_FILE_H
#define ZSV_FILE_H

#include <stdio.h>
#include "file-mem.h"

#ifndef LINEEND
#if defined(WIN32) || defined(_WIN64) || defined(_WIN32)
#define LINEEND "\r\n"
#else
#define LINEEND "\n"
#endif
#endif // LINEEND
/**
 * Get a temp file name. The file is created (empty, mode 0600); the returned
 * value, if any, will have been allocated on the heap, and the caller should
 * `free()`.
 *
 * On POSIX the directory is `$TMPDIR`, falling back to the current directory
 * when it is unset, empty, or unusable (a fallback warns on stderr). On Windows
 * it is `GetTempPath()` (TMP/TEMP/USERPROFILE) with no fallback.
 *
 * @param prefix string with which the resulting file name will be prefixed
 */
char *zsv_get_temp_filename(const char *prefix);

/**
 * Get a temp file name for a caller that creates the file itself with
 * exclusive-create semantics (`O_CREAT|O_EXCL`), such as the toonwriter /
 * json2toon spill store. Same as `zsv_get_temp_filename()` except the
 * placeholder file it creates is removed, so the caller's create succeeds.
 * The caller's `O_EXCL` still fails closed, so hijacking the path means
 * guessing the name within that window -- which is `mkstemp`-random on POSIX,
 * but only weakly unpredictable on Windows, where `GetTempFileName()` derives
 * it from the system time.
 *
 * @param prefix truncated to the 3 chars `zsv_get_temp_filename()` accepts,
 *               so a library's longer fixed prefix is safe to pass through
 */
char *zsv_get_temp_filename_excl(const char *prefix);

/**
 *  Replacement for tmpfile().
 *  Returns filename; file must be manually removed after fclose
 *
 *  @param mode optional mode passed to fopen(); if NULL, defaults to "wb"
 */
FILE *zsv_tmpfile(const char *prefix, char **filename, const char *mode);

/**
 * Check if a file exists and is readable (with fopen + "rb")
 *
 * @param filename
 * @returns: true  (1) if file exists
 */
int zsv_file_exists(const char *filename);

/**
 * Check whether an open stream is a regular file (as opposed to a pipe, FIFO,
 * terminal or other non-seekable source). Returns 0 if it cannot be determined
 */
int zsv_file_is_regular(FILE *f);

/**
 * Check if a file exists and is readable (with fopen + "rb")
 *
 * @param filename
 * @param err      if file is not readbale, *err is set to a code as defined in errno.h
 * @param f_out:   if provided, on success, set to the opened file ptr
 * @returns: true  (1) if file exists and is readable
 */
int zsv_file_readable(const char *filename, int *err, FILE **f_out);

/**
 * Function that is the same as `fwrite()`, but can be used as a callback
 * argument to `zsv_set_scan_filter()`
 *
 * @param FILEp      pointer to a FILE object, cast as a void *, to write to
 * @param buff       pointer to the buffer of data to write
 * @param bytes_read number of bytes in the buffer to write
 */
size_t zsv_filter_write(void *FILEp, unsigned char *buff, size_t bytes_read);

/**
 * Get a file path's directory length and base name
 * Returns the length of the directory portion of the path
 * and the base name portion of the path
 */
size_t zsv_dir_len_basename(const char *filepath, const char **basename);

/**
 * Copy a file. Create any needed directories
 * On error, prints error message and returns non-zero
 */
int zsv_copy_file(const char *src, const char *dest);

/**
 * Copy a file, given source and destination FILE pointers
 * Return error number per errno.h
 */
int zsv_copy_file_ptr(FILE *src, FILE *dest);

/**
 * Copy a file-like, given source and destination handles
 * and read/write functions
 * Return error number per errno.h
 */
int zsv_copy_filelike_ptr(
  FILE *src, size_t (*freadx)(void *restrict ptr, size_t size, size_t nitems, void *restrict stream), FILE *dest,
  size_t (*fwritex)(const void *restrict ptr, size_t size, size_t nitems, void *restrict stream));
/**
 * printf that does nothing. useful in certain circumstances for ignoring
 * errors e.g. opening the same file twice and not dupicating error logs
 */
int zsv_no_printf(void *, const char *format, ...);

#endif
