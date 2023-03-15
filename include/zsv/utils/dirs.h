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

struct zsv_foreach_dirent_ctx {
  const char *parent;
  const char *entry;
  const char *parent_and_entry;
  struct stat stat;

  void *file_ctx;
  void *dir_ctx;

//  int err;
//  const char *root_path; /* the dir_path that zsv_foreach_dirent was first called with */
//  char *current_filepath;

  /* pointer into current_filepath that skips the root_path portion e.g. props.json */
//  const char *current_childpath;

  /* handles for caller's private use */
};

typedef int (*zsv_foreach_dirent_func)(struct zsv_foreach_dirent_ctx *ctx, size_t depth);

/**
 * Recursively process entries (files and folders) in a directory
 *
 * @param ctx: pointer to context
 * @param dir_path: path of directory to begin processing children of
 * @param dir_func: return 0 on success, non-zero on error
 * @param dir_ctx:  pointer passed to dir_func
 * @param file_func:return 0 on success, non-zero on error
 * @param file_ctx: pointer passed to file_ctx
 *
 * returns error
 */
int zsv_foreach_dirent(const char *dir_path,
                        size_t depth,
                        size_t max_depth,
                        zsv_foreach_dirent_func dir_func, void *dir_ctx,
                        zsv_foreach_dirent_func file_func, void *file_ctx
                        );

#endif
