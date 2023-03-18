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

#include <sys/stat.h>

struct zsv_foreach_dirent_handle {
  const char *parent;           /* name of the parent directory */
  const char *entry;            /* file / dir name of current entry being processed */
  const char *parent_and_entry; /* parent + entry separated by file separator */
  const struct stat stat;       /* stat of current entry */

  void *ctx;                    /* caller-provided context to pass to handler */

  unsigned char verbose:1;
  unsigned char is_dir:1;       /* non-zero if this entry is a directory */
  unsigned char no_recurse:1;        /* set to 1 when handling a dir to prevent recursing into it */
  unsigned char _:5;
};

typedef int (*zsv_foreach_dirent_handler)(struct zsv_foreach_dirent_handle *h, size_t depth);

/**
 * Recursively process entries (files and folders) in a directory
 *
 * @param dir_path    : path of directory to begin processing children of
 * @param max_depth   : maximum depth to recurse, or 0 for no maximum
 * @param handler     : caller-provided entry handler. return 0 on success, non-zero on error
 * @param ctx         : pointer passed to the handler
 * @param verbose     : non-zero for verbose output
 *
 * returns error
 */
int zsv_foreach_dirent(const char *dir_path,
                       size_t max_depth,
                       zsv_foreach_dirent_handler handler,
                       void *ctx,
                       char verbose
                       );

#endif
