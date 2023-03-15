/* Copyright (C) 2022 Guarnerix Inc dba Liquidaty - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Matt Wong <matt@guarnerix.com>
 */

/*
 * remove a given file and its cache
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // unlink()
#include <errno.h>

#define ZSV_COMMAND_NO_OPTIONS
#define ZSV_COMMAND rm
#include "zsv_command.h"

#include <zsv/utils/dirs.h>
#include <zsv/utils/cache.h>

/**
 * TO DO: add --orphaned option to remove all orphaned caches
 */
const char *zsv_rm_usage_msg[] = {
  APPNAME ": remove a file and its related cache",
  "",
  "Usage: " APPNAME " [options] <filepath>",
  "  where options may be:",
  "    -v,--verbose: verbose output",
#ifndef NO_STDIN
  "    -f,--force  : do not prompt for confirmation",
#endif
  "    -k,--keep   : do not remove related cache",
  "    -C,--cache  : only remove related cache (not the file)",
  NULL
};

static int zsv_rm_usage(FILE *target) {
  for(int j = 0; zsv_rm_usage_msg[j]; j++)
    fprintf(target, "%s\n", zsv_rm_usage_msg[j]);
  return target == stdout ? 0 : 1;
}

int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int argc, const char *argv[]) {
  int err = 0;
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    err = zsv_rm_usage(stdout);
  else if(argc < 2)
    err = zsv_rm_usage(stderr);
  else {
    const char *filepath = NULL;
    char force = 0;
#ifdef NO_STDIN
    force = 1;
#endif
    char remove_cache = 1;
    char remove_file = 1;
    char verbose = 0;
    for(int i = 1; !err && i < argc; i++) {
      const char *arg = argv[i];
      if(*arg == '-') {
        if(!strcmp(arg, "-v") || !strcmp(arg, "--verbose"))
          verbose = 1;
        else if(!strcmp(arg, "-f") || !strcmp(arg, "--force"))
          force = 1;
        else if(!strcmp(arg, "-k") || !strcmp(arg, "--keep"))
          remove_cache = 0;
        else if(!strcmp(arg, "-C") || !strcmp(arg, "--cache"))
          remove_file = 0;
        else
          err = zsv_printerr(1, "Unrecognized option: %s", arg);
      } else if(!filepath)
        filepath = arg;
      else
        err = zsv_printerr(1, "Unrecognized option: %s", arg);
    }

    if(!err && !filepath)
      err = zsv_rm_usage(stderr);
    else if(remove_file == 0 && remove_cache == 0)
      err = fprintf(stderr, "Nothing to remove\n");

    if(!err) {
      char ok = 1;
#ifndef NO_STDIN
      if(!force) {
        ok = 0;
        if(!remove_file)
          printf("Are you sure you want to remove the entire cache for the file %s?\n",
                 filepath);
        else
          printf("Are you sure you want to remove the file %s%s?\n",
                 filepath,
                 remove_cache ? " and all of its cache contents" : "");
        char buff[64];
        if(fscanf(stdin, "%60s", buff)
           && strchr("Yy", buff[0]))
          ok = 1;
      }
#endif
      if(ok) {
        if(remove_file) {
          if(verbose)
            fprintf(stderr, "Removing %s", filepath);
          err = unlink(filepath);
          if(err) {
            if(err == ENOENT && force)
              err = 0;
            else
              perror(filepath);
          }
        }
        if(!err) {
          unsigned char *cache_dir = zsv_cache_path((const unsigned char *)filepath, NULL, 0);
          if(!cache_dir)
            err = zsv_printerr(ENOMEM, "Out of memory!");
          else if(zsv_dir_exists((const char *)cache_dir)) {
            err = zsv_remove_dir_recursive(cache_dir);
            if(verbose) {
              if(!err)
                fprintf(stderr, "Removed cache %s", cache_dir);
            }
          }
          free(cache_dir);
        }
      }
    }
  }
  return err;
}
