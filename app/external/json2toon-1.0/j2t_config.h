/* json2toon - centralized build/platform configuration. Included first by every
 * library translation unit that touches the platform, so feature-test macros are
 * set before any system header and every OS/compiler divergence lives in one
 * place. Not part of the public API. */
#ifndef JSON2TOON_CONFIG_H
#define JSON2TOON_CONFIG_H

/* fseeko/ftello, fdopen and a 64-bit off_t are POSIX, which glibc hides under
 * -std=c11 (__STRICT_ANSI__); request them, plus 64-bit file offsets on 32-bit
 * POSIX, before any system header is pulled in. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if !defined(_POSIX_C_SOURCE) && !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------- 64-bit file offsets */

/* Spilled arrays can exceed 2 GiB, so seeks need 64-bit offsets, not fseek's
 * `long`. */
#if defined(_WIN32)
#  define J2T_FSEEK(fp, off) (_fseeki64((fp), (long long)(off), SEEK_SET))
#  define J2T_FTELL(fp)      ((int64_t)_ftelli64(fp))
#elif defined(__unix__) || defined(__APPLE__)
#  define J2T_FSEEK(fp, off) (fseeko((fp), (off_t)(off), SEEK_SET))
#  define J2T_FTELL(fp)      ((int64_t)ftello(fp))
#else
#  define J2T_FSEEK(fp, off) (fseek((fp), (long)(off), SEEK_SET))
#  define J2T_FTELL(fp)      ((int64_t)ftell(fp))
#endif

/* ----------------------------------------------------- exclusive temp create */

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <share.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

/* Open a spill temp file by name, failing if it already exists (O_EXCL). This
 * defeats predictable-name symlink/TOCTOU attacks when a caller-supplied
 * get_temp_filename hands back a path in a shared directory (store.c, 4c).
 * Returns a read+write binary FILE* (the store reads spilled bytes back), or
 * NULL with errno set. */
static inline FILE *j2t_fopen_excl(const char *name) {
#if defined(_WIN32)
  int fd = -1;
  if (_sopen_s(&fd, name, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
               _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0 || fd < 0)
    return NULL;
  {
    FILE *fp = _fdopen(fd, "w+b");
    if (!fp) _close(fd);
    return fp;
  }
#else
  FILE *fp;
  int fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return NULL;
  fp = fdopen(fd, "w+b");
  if (!fp) close(fd);
  return fp;
#endif
}

#endif /* JSON2TOON_CONFIG_H */
