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

int zsv_replace_file(const char *src, const char *dest);

#ifdef _WIN32
#include <windows.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#endif

void zsv_win_to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len);

#endif // #ifdef _WIN32

/**
 * get number of cores
 */
unsigned int zsv_get_number_of_cores(void);
#endif // ZSV_OS_H
