/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_DIRS_H
#define ZSV_DIRS_H

/* Maximum length of file name */
#if !defined(FILENAME_MAX)
#   define FILENAME_MAX MAX_PATH
#endif

/* file slash */
#if !defined(FILESLASH)
# ifdef _WIN32
#  define FILESLASH '\\'
# else
#  define FILESLASH '/'
# endif
#endif

/**
 * Get os-independent configuration file path
 * prefix should be determined at compile time e.g. /usr/local or ""
 * @return length written to buff, or 0 if failed
 */
size_t zsv_get_config_dir(char* buff, size_t buffsize, const char *prefix);

/**
 * Get the path of the current executable
 */
size_t zsv_get_executable_path(char* buff, size_t buffsize);

/**
 * Check if a directory exists
 * return true (non-zero) or false (zero)
 */
int zsv_dir_exists(const char *path);

/**
 * Make a directory, as well as any intermediate dirs
 * return zero on success
 */
int zsv_mkdirs(const char *path, char path_is_filename);

/**
 * Recursively remove a directory and all of its contents
 * return zero on success
 */
int zsv_remove_dir_recursive(const unsigned char *path);

#endif
