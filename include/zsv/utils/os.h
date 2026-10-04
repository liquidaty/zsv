/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_OS_H
#define ZSV_OS_H

// the macros below expand to fopen()/remove(); a consumer including only this
// header would otherwise get an implicit declaration
#include <stdio.h>

void zsv_perror(const char *);

/**
 * zsv_fopen(): same as normal fopen(), except on Win it also works with long filenames
 */
#ifndef _WIN32
#define zsv_fopen fopen
#else
FILE *zsv_fopen(const char *fname, const char *mode);
// NULL when original_path is empty, short and !always_prefix, not UTF-8, or on error
char *zsv_ensureLongPathPrefix(const char *original_path, unsigned char always_prefix);
#endif

/**
 * zsv_mkstemp(): same as normal mkstemp(), which wasi-libc does not have
 * (wasm has no temp directory concept). Creates and opens a file whose name is
 * `tmpl` with its trailing 'X's (at least 6) replaced; `tmpl` is modified in
 * place. Returns an open fd, or -1 with errno set.
 *
 * A function rather than a macro aliasing mkstemp(): glibc hides mkstemp()
 * under -std=cNN, so a macro would leave this header un-self-sufficient for a
 * consumer that has not set a feature-test macro.
 */
int zsv_mkstemp(char *tmpl);

/**
 * zsv_remove(): same as normal remove()
 but for files only, and on Win it also works with long filenames
 */
#ifndef _WIN32
#define zsv_remove remove
#else
int zsv_remove_winlp(const char *path_utf8);
#define zsv_remove zsv_remove_winlp
#endif

/**
 * zsv_replace_file(): rename src over dest, copying across devices if needed.
 * Returns 0, or an errno value that errno is also set to. On Windows both paths
 * are UTF-8 and may exceed MAX_PATH; a path that is not UTF-8 gives EILSEQ
 */
int zsv_replace_file(const char *src, const char *dest);

/**
 * zsv_same_file(): whether both paths name the same file, by device and file id, so
 * links and differently-spelled paths match. Returns 1 if they do, 0 if they do not
 * (including when either does not exist), or -1 if that cannot be determined
 */
int zsv_same_file(const char *a, const char *b);

/**
 * zsv_copy_permissions(): give file `to` the permission bits of existing file `from`,
 * and on POSIX its group. Where the group cannot be set, `to` gets no more group
 * access than others have, so no other group gains access. On Windows the only bit
 * is the read-only attribute. Use before `to` replaces `from`. Returns 0, or an
 * errno value (on Windows, EILSEQ for a path that is not UTF-8)
 */
int zsv_copy_permissions(const char *from, const char *to);

/**
 * zsv_create_new_file(): create an empty file at `path`, which must not exist, with
 * the permissions the system gives a new file (umask, directory defaults). Returns 0,
 * or an errno value (EEXIST if `path` exists; on Windows, EILSEQ for a path that is
 * not UTF-8)
 */
int zsv_create_new_file(const char *path);

/**
 * zsv_fsync(): flush a file descriptor's data to storage; 0 on success, else -1
 * with errno set. A function for the reason zsv_mkstemp() is one
 */
int zsv_fsync(int fd);

/**
 * zsv_final_path(): the path of the file `path` names after following symbolic
 * links, so that replacing it replaces that file rather than the link. A copy of
 * `path` if it does not exist, or on Windows. Returns heap memory the caller frees,
 * or NULL with errno set (ENOENT for a link to nothing)
 */
char *zsv_final_path(const char *path);

/**
 * ZSV_STDIN_IS_TTY(): nonzero when stdin is an interactive terminal
 */
#ifndef _WIN32
#include <unistd.h>
#define ZSV_STDIN_IS_TTY() isatty(fileno(stdin))
#else
#include <io.h>
#define ZSV_STDIN_IS_TTY() _isatty(_fileno(stdin))
#endif

#ifdef _WIN32
#include <windows.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#endif

// converts UTF-8 `path` into wbuf, cutting it off at PATH_MAX; wbuf is empty if the path
// is not UTF-8. For short names such as a DLL's; file paths go through the long-path helpers
void zsv_win_to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len);

#endif // #ifdef _WIN32

/**
 * get number of cores
 */
unsigned int zsv_get_number_of_cores(void);
#endif // ZSV_OS_H
