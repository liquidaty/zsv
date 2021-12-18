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
#include <zsv/utils/os.h>
#include <zsv/utils/dirs.h>

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
size_t get_config_dir(char* buff, size_t buffsize, const char *prefix) {
#if defined(_WIN32)
  const char *env = prefix;
  const char *env_val = getenv(env);
  if(!(env_val && *env_val))
    env_val = getenv("LOCALAPPDATA");
  int written = snprintf(buff, buffsize, "%s", env_val);
#elif defined(__EMSCRIPTEN__)
  int written = snprintf(buff, buffsize, "/tmp");
#else
  int written = snprintf(buff, buffsize, "%s/etc", prefix ? prefix : "");
#endif
  if(written > 0 && ((size_t)written) < buffsize)
    return chop_slash(buff, written);
  return 0;
}

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
size_t get_app_data_dir(char *buff, size_t buffsize, const char *prefix_or_env) {
#ifdef _WIN32
  const char *env = prefix_or_env ? prefix_or_env : "LOCALAPPDATA"; // : "APPDATA";
  int written = snprintf(buff, buffsize, "%s", getenv(env));
#else
  int written = snprintf(buff, buffsize, "%s/var", prefix_or_env ? prefix_or_env : "");
#endif
  if(written > 0 && ((size_t)written) < buffsize)
    return chop_slash(buff, written);
  return 0;
}

/**
 * Get temp directory
 * @return length written to buff, or 0
 */
size_t get_temp_dir(char *buff, size_t buffsize) {
#ifdef _WIN32
  int written = snprintf(buff, buffsize, "%s", getenv("TEMP"));
#else
  int written = snprintf(buff, buffsize, "%s", getenv("TMPDIR"));
#endif
  if(written > 0 && ((size_t)written) < buffsize)
    return chop_slash(buff, written);
  return 0;
}


#if defined(_WIN32)
size_t get_executable_path(char* buff, size_t buffsize) {
  return GetModuleFileNameA(NULL, buff, (DWORD)buffsize);
}

#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
size_t get_executable_path(char* buff, size_t bufflen) {
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
size_t get_executable_path(char* buff, size_t buffsize) {
  buffsize = readlink("/proc/self/exe", buff, buffsize - 1);
  buff[buffsize] = '\0';
  return buffsize;
}
#elif defined(__FreeBSD__)
#include <sys/stat.h>
#include <sys/sysctl.h>
size_t get_executable_path(char* buff, size_t buffsize) {
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

