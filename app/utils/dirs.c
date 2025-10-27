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
#include <errno.h>
#include <zsv/utils/os.h>
#ifndef ZSV_NO_JQ
#include <zsv/utils/json.h>
#include <zsv/utils/jq.h>
#endif
#include <zsv/utils/dirs.h>
#include <zsv/utils/string.h>
#include <unistd.h> // unlink
#include <sys/stat.h>
#ifndef ZSV_NO_JQ
#include <yajl_helper/yajl_helper.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include "win/io.c"
#endif

/**
 * Most of these functions require the caller to provide a buffer, in which case
 * the buffer size should be at least FILENAME_MAX
 */

static size_t chop_slash(char *buff, size_t len) {
  if (buff[len - 1] == '\\' || buff[len - 1] == '/') {
    buff[len - 1] = '\0';
    len--;
  }
  return (size_t)len;
}

/**
 * Get os-independent configuration file path
 * prefix should be determined at compile time e.g. /usr/local or ""
 * @return length written to buff, or 0 if failed
 */
size_t zsv_get_config_dir(char *buff, size_t buffsize, const char *prefix) {
#if defined(_WIN32)
  const char *env_val = getenv("ZSV_CONFIG_DIR");
  (void)(prefix);
  //  if(!(env_val && *env_val))
  //    env_val = getenv(prefix);
  if (!(env_val && *env_val))
    env_val = getenv("LOCALAPPDATA");
  if (!(env_val && *env_val))
    env_val = "C:\\temp";
  int written = snprintf(buff, buffsize, "%s", env_val);
#elif defined(__EMSCRIPTEN__)
  (void)(prefix);
  int written = snprintf(buff, buffsize, "/tmp");
#else
  int written;
  const char *env_val = getenv("ZSV_CONFIG_DIR");
  if (env_val && *env_val)
    written = snprintf(buff, buffsize, "%s", env_val);
  else
    written = snprintf(buff, buffsize, "%s/etc", prefix ? prefix : "");
#endif
  if (written > 0 && ((size_t)written) < buffsize)
    return chop_slash(buff, written);
  return 0;
}

/**
 * Check if a directory exists
 * return true (non-zero) or false (zero)
 */
#ifdef WIN32
#include "win/dir_exists_longpath.c"
#endif
int zsv_dir_exists(const char *path) {
#ifdef WIN32
  if (strlen(path) >= MAX_PATH)
    return zsv_dir_exists_winlp(path);

  // TO DO: support win long filepath prefix
  // TO DO: work properly if dir exists but we don't have permission
  wchar_t wpath[MAX_PATH];
  mbstowcs(wpath, path, MAX_PATH);

  DWORD attrs = GetFileAttributesW(wpath);
  if (attrs == INVALID_FILE_ATTRIBUTES)
    // Could check GetLastError() to see if it's a permission issue vs. not-found
    return 0;

  // If it has the directory attribute, it's presumably a directory
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

#else
  struct stat path_stat;
  if (!stat(path, &path_stat))
    return S_ISDIR(path_stat.st_mode);
  return 0;
#endif
}

/**
 * Make a directory, as well as any intermediate dirs
 * return zero on success
 */
#ifdef WIN32
#include "win/mkdir_longpath.c"
#define zsv_mkdir zsv_mkdir_winlp
#else
#define zsv_mkdir mkdir
#endif

int zsv_mkdirs(const char *path, char path_is_filename) {
  // int rc = 0;
  if (!path || !*path)
    return -1;
  size_t len = strlen(path);
  if (len < 1)
    return -1;

  char *tmp = strdup(path);
  if (!tmp) {
    perror(path);
    return -1;
  }

  if (len && strchr("/\\", tmp[len - 1]))
    tmp[--len] = 0;

  int offset = 0;
#ifdef WIN32
  if (len > 1) {
    // starts with two slashes
    if (strchr("/\\", tmp[0]) && strchr("/\\", tmp[1])) {
      offset = 2;
      // find the next slash
      char *path_end = tmp + 3;
      while (*path_end && !strchr("/\\", *path_end))
        path_end++;
      if (*path_end)
        path_end++;
      if (*path_end)
        offset = path_end - tmp;
      else {
        fprintf(stderr, "Invalid path: %s\n", path);
        free(tmp);
        return -1;
      }
    }
    // starts with *:
    else if (tmp[1] == ':')
      offset = 2;
  }
#else
  offset = 1;
#endif

  // TO DO: first find the longest subdir that exists, in *reverse* order so as
  // to properly handle case where no access to intermediate dir,
  // and then only start mkdir from there
  int last_dir_exists_rc = 0;
  int last_errno = -1;
  for (char *p = tmp + offset; /* !rc && */ *p; p++) {
    if (strchr("/\\", *p)) {
      char tmp_c = p[1];
      p[0] = FILESLASH;
      p[1] = '\0';
      if (*tmp && !(last_dir_exists_rc = zsv_dir_exists(tmp))) {
        if (zsv_mkdir(tmp
#ifndef WIN32
                      ,
                      S_IRWXU
#endif
                      )) {
          if (errno == EEXIST)
            last_dir_exists_rc = 1;
          else { // errno could be EEXIST if we have no permissions to an intermediate directory
            last_errno = errno;
            perror(tmp);
            //          rc = -1;
          }
        } else
          last_dir_exists_rc = 1;
      }
      p[1] = tmp_c;
    }
  }

  if (/* !rc && */ path_is_filename == 0 && *tmp && !(last_dir_exists_rc = zsv_dir_exists(tmp))) {
    if (zsv_mkdir(tmp
#ifndef WIN32
                  ,
                  S_IRWXU
#endif
                  )) {
      if (errno == EEXIST)
        last_dir_exists_rc = 1;
      else {
        last_errno = errno;
        perror(tmp);
        // rc = -1;
      }
    } else
      last_dir_exists_rc = 1;
  }

  free(tmp);
  return last_dir_exists_rc ? 0 : last_errno ? last_errno : -1;
}

#if defined(_WIN32)
size_t zsv_get_executable_path(char *buff, size_t buffsize) {
  return GetModuleFileNameA(NULL, buff, (DWORD)buffsize);
}

#elif defined(__APPLE__)
#include <mach-o/dyld.h>
size_t zsv_get_executable_path(char *buff, size_t bufflen) {
  uint32_t pathlen = bufflen;
  if (!_NSGetExecutablePath(buff, &pathlen)) {
    char real[FILENAME_MAX];
    if (realpath(buff, real) != NULL && strlen(real) < bufflen) {
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
size_t zsv_get_executable_path(char *buff, size_t buffsize) {
  buffsize = readlink("/proc/self/exe", buff, buffsize - 1);
  buff[buffsize] = '\0';
  return buffsize;
}
#elif defined(__FreeBSD__)
#include <sys/stat.h>
#include <sys/sysctl.h>
size_t zsv_get_executable_path(char *buff, size_t buffsize) {
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
// TODO: Add support for this OS!
#endif /* end of: #if defined(_WIN32) */

/**
 * Get current user's home dir, without trailing slash
 * On win, any backslashes are replaced with fwd slash
 *   ex: zsv_get_home_dir(char[MAX_PATH], MAX_PATH)
 * returns 0 if no home dir could be found
 * returns > 0 and < bufflen on success
 * returns > 0 and >= bufflen if buffer was too small
 */
int zsv_get_home_dir(char *buff, size_t bufflen) {
  int written = 0;
  if (getenv("HOME"))
    written = snprintf(buff, bufflen, "%s", getenv("HOME"));
#if defined(WIN32) || defined(_WIN32)
  if (!written && getenv("HOMEDRIVE") && getenv("HOMEPATH"))
    written = snprintf(buff, bufflen, "%s%s", getenv("HOMEDRIVE"), getenv("HOMEPATH"));
#endif
  if (written > 0 && ((size_t)written) < bufflen) {
    if (buff[written - 1] == '\\' || buff[written - 1] == '/') {
      buff[written - 1] = '\0';
      written--;
    }
  }
#if defined(WIN32) || defined(_WIN32)
  for (int i = 0; i < written; i++)
    if (buff[i] == '\\')
      buff[i] = '/';
#endif
  return written;
}

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
  if (!RemoveDirectoryA(path)) {
    zsv_perror(path);
    // zsv_win_printLastError();
    *err = 1;
  }
#else
  if (remove(path)) {
    perror(path);
    *err = 1;
  }
#endif
  return *err;
}

static int zsv_foreach_dirent_remove(struct zsv_foreach_dirent_handle *h, size_t depth) {
  (void)(depth);
  if (!h->is_dir) { // file
    if (h->parent_and_entry) {
      if (zsv_remove(h->parent_and_entry)) {
        perror(h->parent_and_entry); // "Unable to remove file");
        return 1;
      }
    }
  } else { // dir
    if (h->parent_and_entry) {
      struct dir_path *dn = calloc(1, sizeof(*dn));
      if (!dn) {
        fprintf(stderr, "Out of memory!\n");
        return 1;
      }
      dn->path = strdup(h->parent_and_entry);
      dn->next = *((struct dir_path **)h->ctx);
      *((struct dir_path **)h->ctx) = dn;
    }
  }
  return 0;
}

#ifdef _WIN32
#include "win/foreach_dirent_longpath.c"
#else
// return error
static int zsv_foreach_dirent_aux(const char *dir_path, size_t depth, size_t max_depth,
                                  zsv_foreach_dirent_handler handler, void *ctx, char verbose) {
  int err = 0;
  if (!dir_path)
    return 1;

  if (max_depth > 0 && depth >= max_depth)
    return 0;

  DIR *dr;
  if ((dr = opendir(dir_path))) {
    struct dirent *de;
    while ((de = readdir(dr)) != NULL) {
      if (!*de->d_name || !strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
        continue;
      char *tmp;
      asprintf(&tmp, "%s%c%s", dir_path, FILESLASH, de->d_name);
      if (!tmp)
        fprintf(stderr, "Out of memory!\n"), err = 1;
      else {
        struct zsv_foreach_dirent_handle h = {0};
        h.verbose = verbose;
        stat(tmp, (struct stat *)&h.stat);
        h.parent = dir_path;
        h.entry = de->d_name;
        h.parent_and_entry = tmp;
        h.ctx = ctx;
        char is_dir = h.stat.st_mode & S_IFDIR ? 1 : 0;
        h.is_dir = is_dir;
        if (handler)
          handler(&h, depth + 1);

        if (is_dir && !h.no_recurse)
          // recurse!
          err = zsv_foreach_dirent_aux(tmp, depth + 1, max_depth, handler, ctx, verbose);
        free(tmp);
      }
    }
    closedir(dr);
  }
  return err;
}
#endif

int zsv_foreach_dirent(const char *dir_path, size_t max_depth, zsv_foreach_dirent_handler handler, void *ctx,
                       char verbose) {
  return zsv_foreach_dirent_aux(dir_path, 0, max_depth, handler, ctx, verbose);
}

/**
 * Remove a directory and all of its contents
 */
int zsv_remove_dir_recursive(const unsigned char *path) {
  // we will delete all files first, then
  // delete directories in the reverse order we received them
  struct dir_path *reverse_dirs = NULL;
  int err = zsv_foreach_dirent((const char *)path, 0, zsv_foreach_dirent_remove, &reverse_dirs, 0);

  // unlink each dir
  for (struct dir_path *dn = reverse_dirs; !err && dn; dn = dn->next)
    rmdir_w_msg(dn->path, &err);

  // free each dir
  for (struct dir_path *next, *dn = reverse_dirs; !err && dn; dn = next) {
    next = dn->next;
    free(dn->path);
    free(dn);
  }
  if (!err)
    rmdir_w_msg((const char *)path, &err);

  return err;
}

#ifndef ZSV_NO_JQ
#include "dirs_to_json.c"

#include "dirs_from_json.c"

#endif
