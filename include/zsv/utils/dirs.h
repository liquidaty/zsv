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
size_t get_config_dir(char* buff, size_t buffsize, const char *prefix);

/**
 * Get the path of the current executable
 */
size_t get_executable_path(char* buff, size_t buffsize);

/**
 * Get global app data dir for application data that is variable
 * local or not-local should be a compile-time option;
 * the default for `make install` is local and for packaged distributions is non-local
 * @param prefix_or_env should be prefix on unix-like systems, or LOCALAPPDATA or APPDATA on
 * Windows systems. If none is provided, defaults to /usr/local (unix) or LOCALAPPDATA (Windows)
 * @return length written to buff, or 0 if failed
 *
 * (or overridden via configure --localstatedir in which case this function should not be called)
 */
size_t get_app_data_dir(char *buff, size_t buffsize, const char *prefix_or_env);

/**
 * Get temp directory
 * @return length written to buff, or 0
 */
size_t get_temp_dir(char *buff, size_t buffsize);

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
