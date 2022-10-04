/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <zsv/utils/os.h>
#include <zsv/utils/dirs.h>
#include <unistd.h> // unlink
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/**
 * Most of these functions require the caller to provide a buffer, in which case
 * the buffer size should be FILENAME_MAX
 */

static size_t chop_slash(char* buff, size_t len) {
  if(buff[len-1] == '\\' || buff[len-1] == '/') {
    buff[len-1] = '\0';
    len--;
  }
  return (size_t) len;
}

/**
 * Get os-independent configuration file path
 * prefix should be determined at compile time e.g. /usr/local or ""
 * @return length written to buff, or 0 if failed
 */
size_t zsv_get_config_dir(char* buff, size_t buffsize, const char *prefix) {
#if defined(_WIN32)
  const char *env_val = getenv("ZSV_CONFIG_DIR");
  (void)(prefix);
  //  if(!(env_val && *env_val))
  //    env_val = getenv(prefix);
  if(!(env_val && *env_val))
    env_val = getenv("LOCALAPPDATA");
  if(!(env_val && *env_val))
    env_val = "C:\\temp";
  int written = snprintf(buff, buffsize, "%s", env_val);
#elif defined(__EMSCRIPTEN__)
  int written = snprintf(buff, buffsize, "/tmp");
#else
  int written;
  const char *env_val = getenv("ZSV_CONFIG_DIR");
  if(env_val && *env_val)
    written = snprintf(buff, buffsize, "%s", env_val);
  else
    written = snprintf(buff, buffsize, "%s/etc", prefix ? prefix : "");
#endif
  if(written > 0 && ((size_t)written) < buffsize)
    return chop_slash(buff, written);
  return 0;
}

/**
 * Check if a directory exists
 * return true (non-zero) or false (zero)
 */
int zsv_dir_exists(const char *path) {
  struct stat path_stat;
  if(!stat(path, &path_stat))
    return S_ISDIR(path_stat.st_mode);
  return 0;
}

/**
 * Make a directory, as well as any intermediate dirs
 * return zero on success
 */
int zsv_mkdirs(const char *path, char path_is_filename) {
  char *p = NULL;
  int rc = 0;

  size_t len = strlen(path);
  if(len < 1 || len > FILENAME_MAX)
    return -1;

  char *tmp = strdup(path);
  if(len && strchr("/\\", tmp[len - 1]))
    tmp[--len] = 0;

  int offset = 0;
#ifdef WIN32
  if(len > 1) {
    // starts with two slashes
    if(strchr("/\\", tmp[0]) && strchr("/\\", tmp[1]))
      offset = 2;

    // starts with *:
    else if(tmp[1] == ':')
      offset = 2;
  }
#else
  offset = 1;
#endif

  for(p = tmp + offset; !rc && *p; p++)
    if(strchr("/\\", *p)) {
      char tmp_c = p[1];
      p[0] = FILESLASH;
      p[1] = '\0';
      if(*tmp && !zsv_dir_exists(tmp) && mkdir(tmp
#ifndef WIN32
               , S_IRWXU
#endif
               ))
        rc = -1;
      else
        p[1] = tmp_c;
    }

  if(!rc && path_is_filename == 0 && *tmp && !zsv_dir_exists(tmp)
     && mkdir(tmp
#ifndef WIN32
              , S_IRWXU
#endif
              ))
    rc = -1;

  free(tmp);
  return rc;
}


#if defined(_WIN32)
size_t zsv_get_executable_path(char* buff, size_t buffsize) {
  return GetModuleFileNameA(NULL, buff, (DWORD)buffsize);
}

#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
size_t zsv_get_executable_path(char* buff, size_t bufflen) {
  uint32_t pathlen = bufflen;
  if(!_NSGetExecutablePath(buff, &pathlen)) {
    char real[FILENAME_MAX];
    if(realpath(buff, real) != NULL && strlen(real) < bufflen) {
      bufflen = strlen(real);
      memcpy(buff, real, bufflen);
      buff[bufflen] = '\0';
    } else
      bufflen = pathlen;
    return bufflen;
  }
  return 0;
}
#elif defined(__linux__) || defined(__EMSCRIPTEN__)
  #include <unistd.h>
size_t zsv_get_executable_path(char* buff, size_t buffsize) {
  buffsize = readlink("/proc/self/exe", buff, buffsize - 1);
  buff[buffsize] = '\0';
  return buffsize;
}
#elif defined(__FreeBSD__)
#include <sys/stat.h>
#include <sys/sysctl.h>
size_t zsv_get_executable_path(char* buff, size_t buffsize) {
  int mib[4];
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PATHNAME;
  mib[3] = -1;
  sysctl(mib, 4, buff, &buffsize, NULL, 0);
  buff[buffsize] = '\0';
  return buffsize;
}
#else

to do: add support for this OS!;

#endif /* end of: #if defined(_WIN32) */

struct remove_dir_ctx {
  int err;
  struct dir_path *dirs;
};

struct dir_path {
  struct dir_path *next;
  char *path;
};

/**
 * remove an empty directory; on error print msg to stderr
 * return 0 on success
 */
static int rmdir_w_msg(const char *path, int *err) {
#ifdef WIN32
  if(!RemoveDirectoryA(path)) {
    zsv_win_printLastError();
    *err = 1;
  }
#else
  if(remove(path)) {
    perror(path);
    *err = 1;
  }
#endif
  return *err;
}

static void remove_files_collect_dirs(struct remove_dir_ctx *ctx, const char *path) {
  // delete all files, collect dir names in reverse order
  DIR *dr;
  struct dir_path *previous_dir = ctx->dirs;
  struct dir_path *most_recent_dir = NULL;
  if((dr = opendir(path))) {
    struct dirent *de;
    while((de = readdir(dr)) != NULL) {
      if(!*de->d_name || !strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
        continue;
      char *tmp;
      asprintf(&tmp, "%s%c%s", path, FILESLASH, de->d_name);
      if(!tmp)
        fprintf(stderr, "Out of memory!\n"), ctx->err = 1;
      else {
        struct stat s;
        stat(tmp, &s);
        if (s.st_mode & S_IFDIR) { // it's a dir. save for later
          struct dir_path *dn = calloc(1, sizeof(*dn));
          if(!dn)
            fprintf(stderr, "Out of memory!\n"), ctx->err = 1;
          else {
            most_recent_dir = dn;
            dn->path = tmp;
            dn->next = ctx->dirs;
            ctx->dirs = dn;
          }
        } else { // not a dir. try to remove
          if(unlink(tmp)) {
            perror(tmp); // "Unable to remove file");
            // fprintf(stderr, "%s\n", tmp);
            ctx->err = 1;
          }
          free(tmp);
        }
      }
    }
    closedir(dr);
  }

  // process all sub-dirs that we just collected
  for(struct dir_path *dn = most_recent_dir; dn && dn != previous_dir; dn = dn->next)
    remove_files_collect_dirs(ctx, dn->path);
}

/**
 * Remove a directory and all of its contents
 */
int zsv_remove_dir_recursive(const unsigned char *path) {
  const char *cpath = (void *)path;
  struct remove_dir_ctx ctx = { 0 };
  // we will delete all files first, then
  // delete directories in the reverse order we received them
  remove_files_collect_dirs(&ctx, cpath);

  // unlink and free each dir
  for(struct dir_path *next, *dn = ctx.dirs; dn; dn = next) {
    next = dn->next;
    rmdir_w_msg(dn->path, &ctx.err);
    free(dn->path);
    free(dn);
  }
  if(!ctx.err)
    rmdir_w_msg(cpath, &ctx.err);

  return ctx.err;
}
