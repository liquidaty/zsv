/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_FILE_H
#define ZSV_FILE_H

#ifndef LINEEND
# if defined(WIN32) || defined(_WIN64) || defined(_WIN32)
#  define LINEEND "\r\n"
# else
#  define LINEEND "\n"
# endif
#endif // LINEEND

char *zsv_get_temp_filename(const char *prefix);

/**
 * Check if a file exists and is readable (with fopen + "rb")
 * @param filename
 * @param err      if file is not readbale, *err is set to a code as defined in errno.h
 * @param f_out:   if provided, on success, set to the opened file ptr

 * @returns: true  (1) if file exists and is readable
 */
int zsv_file_readable(const char *filename, int *err, FILE **f_out);

#endif
