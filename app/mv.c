/* Copyright (C) 2022 Guarnerix Inc dba Liquidaty - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Matt Wong <matt@guarnerix.com>
 */

/*
 * Move a given file and its cache as follows:
 * 1. check that the destination file doesn't exist. if it does, exit with an error
 * 2. if a cache exists, check that the destination file cache dir doesn't exist. if it does, exit with an error
 * 3. move the file. if it fails, exit with an error
 * 4. move the cache, if it exists. if it fails, attempt to move the file back to its original location, and exit with an error
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // unlink()
#include <errno.h>

#define ZSV_COMMAND_NO_OPTIONS
#define ZSV_COMMAND mv
#include "zsv_command.h"

#include <zsv/utils/dirs.h>
#include <zsv/utils/file.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/os.h>

const char *zsv_mv_usage_msg[] = {
  APPNAME ": move a file and its related cache",
  "",
  "Usage: " APPNAME " [options] <source> <destination>",
  "  where options may be:",
  "    -v,--verbose: verbose output",
  "    -C,--cache  : only move related cache (not the file)",
  NULL
};

static int zsv_mv_usage(FILE *target) {
  for(int j = 0; zsv_mv_usage_msg[j]; j++)
    fprintf(target, "%s\n", zsv_mv_usage_msg[j]);
  return target == stdout ? 0 : 1;
}

int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int argc, const char *argv[]) {
  int err = 0;
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    err = zsv_mv_usage(stdout);
  else if(argc < 2)
    err = zsv_mv_usage(stderr);
  else {
    const char *source = NULL;
    const char *dest = NULL;

    char move_file = 1;
    char verbose = 0;
    for(int i = 1; !err && i < argc; i++) {
      const char *arg = argv[i];
      if(*arg == '-') {
        if(!strcmp(arg, "-v") || !strcmp(arg, "--verbose"))
          verbose = 1;
        else if(!strcmp(arg, "-C") || !strcmp(arg, "--cache"))
          move_file = 0;
        else
          err = zsv_printerr(1, "Unrecognized option: %s", arg);
      } else if(!source)
        source = arg;
      else if(!dest)
        dest = arg;
      else
        err = zsv_printerr(1, "Unrecognized option: %s", arg);
    }

    if(!err) {
      unsigned char *source_cache_dir = zsv_cache_path((const unsigned char *)source, NULL, 0);
      unsigned char *dest_cache_dir = zsv_cache_path((const unsigned char *)dest, NULL, 0);
      if(!source || !dest) {
        err = zsv_mv_usage(stderr);
      } else if(move_file && !zsv_file_exists(source)) {
        err = errno = ENOENT;
        perror(source);
      } else if(move_file && zsv_file_exists(dest)) {
        err = errno = EEXIST;
        perror(dest);
      } else if(zsv_dir_exists((const char *)source_cache_dir) && zsv_dir_exists((const char *)dest_cache_dir)) {
        err = errno = EEXIST;
        perror((char*)dest_cache_dir);
        fprintf(stderr, "Use `mv --cache %s <destination>` to move or `rm --cache %s` to remove, then try again\n",
                dest, dest);
      } else if(move_file && (verbose ? fprintf(stderr, "Renaming files\n") : 1)
                && zsv_replace_file(source, dest)) {
        err = errno;
        fprintf(stderr, "%s -> %s: ", source, dest);
        zsv_perror(NULL);
      } else if(zsv_dir_exists((const char *)source_cache_dir) && (verbose ? fprintf(stderr, "Moving caches\n") : 1)
                && rename(// rename(): not sure will work on Win with NFS dirs...
                          (char*)source_cache_dir, (char*)dest_cache_dir)
                ) {
        err = errno;
        fprintf(stderr, "%s -> %s: ", source_cache_dir, dest_cache_dir);
        perror(NULL);
        if(rename(dest, source)) { // try to revert the prior rename. see above re Win + NFS
          fprintf(stderr, "%s -> %s: ", dest, source);
          perror(NULL);
        }
      }
      free(source_cache_dir);
      free(dest_cache_dir);
    }
  }
  return err;
}
