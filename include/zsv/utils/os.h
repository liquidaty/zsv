/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_OS_H
#define ZSV_OS_H

void zsv_perror(const char *);

/**
 * zsv_fopen(): same as normal fopen(), except on Win it also works with long filenames
 */
#ifndef _WIN32
#define zsv_fopen fopen
#else
FILE *zsv_fopen(const char *fname, const char *mode);
#endif

#ifndef _WIN32

int zsv_replace_file(const char *src, const char *dest);

#else

#include <windows.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#endif

void zsv_win_to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len);

int zsv_replace_file(const void *src, const void *dest);

/**
 * Windows does not have perror(), so we define our own printLastError()
void zsv_win_printLastError(const char *prefix);
 */

#endif

#endif
