// Unit test for the zsv/utils/os file helpers zsv_replace_file(), zsv_create_new_file(),
// zsv_copy_permissions() and zsv_same_file() on short paths, on paths longer than the
// Windows MAX_PATH, and (on Windows) on names that are not valid UTF-8.
// Usage: test_replace_file <tmp dir>. Exit status is the number of failed checks.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zsv/utils/os.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <wchar.h>
#include "../utils/win/io.h"
int zsv_dir_exists_winlp(const char *path_utf8); // win/dir_exists_longpath.c
#define TEST_MKDIR(p) _mkdir(p)
#define TEST_RMDIR(p) _rmdir(p)
#define TEST_ABSPATH(p) _fullpath(NULL, p, 0)
#else
#include <unistd.h>
#define TEST_MKDIR(p) mkdir(p, 0777)
#define TEST_RMDIR(p) rmdir(p)
#define TEST_ABSPATH(p) realpath(p, NULL)
#endif

// length at which the fixed buffers zsv once converted Windows paths into cut a path off
#define TEST_TRUNCATED_LEN 259

// a directory component and file names long enough that dir/file passes the
// 260-character Windows MAX_PATH while dir alone stays short enough for _mkdir()
#define TEST_LONG_DIR_LEN 100
#define TEST_LONG_FILE_LEN 200
#define TEST_PATH_MAX 1024

static int failures;

#define CHECK(cond, ...)                                                                                               \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      failures++;                                                                                                      \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                             \
      fprintf(stderr, __VA_ARGS__);                                                                                    \
      fputc('\n', stderr);                                                                                             \
    }                                                                                                                  \
  } while (0)

// snprintf() into array `buf`, failing the check if the result does not fit
#define FORMAT_PATH(buf, ...)                                                                                          \
  CHECK((size_t)snprintf(buf, sizeof(buf), __VA_ARGS__) < sizeof(buf), "path too long: %s", buf)

// path = dir + "/" + `len` copies of `c` + suffix
static void make_name(char *path, size_t size, const char *dir, char c, size_t len, const char *suffix) {
  char name[TEST_LONG_FILE_LEN + 1];
  memset(name, c, len);
  name[len] = '\0';
  CHECK((size_t)snprintf(path, size, "%s/%s%s", dir, name, suffix) < size, "path too long: %s", path);
}

static int write_file(const char *path, const char *content) {
  FILE *f = zsv_fopen(path, "wb");
  if (!f)
    return -1;
  size_t n = strlen(content);
  int rc = fwrite(content, 1, n, f) == n ? 0 : -1;
  if (fclose(f))
    rc = -1;
  return rc;
}

// 1 if the file at `path` holds exactly `content`, 0 if it differs, -1 if it cannot be opened
static int file_is(const char *path, const char *content) {
  FILE *f = zsv_fopen(path, "rb");
  if (!f)
    return -1;
  char buf[64];
  size_t n = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  return n == strlen(content) && !memcmp(buf, content, n);
}

static int file_exists(const char *path) {
  FILE *f = zsv_fopen(path, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

// replace, create-new, copy-permissions and same-file in directory `dir`, with file
// names of `len` characters
static void check_dir(const char *dir, size_t len) {
  char src[TEST_PATH_MAX], dst[TEST_PATH_MAX], fresh[TEST_PATH_MAX];
  make_name(src, sizeof(src), dir, 's', len, ".csv");
  make_name(dst, sizeof(dst), dir, 'd', len, ".csv");
  make_name(fresh, sizeof(fresh), dir, 'n', len, ".csv");
  zsv_remove(src); // left behind by an interrupted run
  zsv_remove(dst);
  zsv_remove(fresh);

  // replace onto an absent destination, then over an existing one
  CHECK(write_file(src, "one") == 0, "cannot write %s", src);
  int err = zsv_replace_file(src, dst);
  CHECK(err == 0, "replace onto new file (len %zu): %s", len, strerror(err));
  CHECK(file_is(dst, "one") == 1, "destination content wrong (len %zu)", len);
  CHECK(!file_exists(src), "source still present (len %zu)", len);

  CHECK(write_file(src, "two") == 0, "cannot write %s", src);
  err = zsv_replace_file(src, dst);
  CHECK(err == 0, "replace over existing file (len %zu): %s", len, strerror(err));
  CHECK(file_is(dst, "two") == 1, "destination not overwritten (len %zu)", len);
  CHECK(!file_exists(src), "source still present after overwrite (len %zu)", len);

  errno = 0;
  err = zsv_replace_file(src, dst);
  CHECK(err == ENOENT, "missing source (len %zu): want ENOENT, got %d (%s)", len, err, strerror(err));
  CHECK(errno == err, "errno %d after a failed replace differs from the result %d (len %zu)", errno, err, len);
  CHECK(file_is(dst, "two") == 1, "destination changed by a failed replace (len %zu)", len);

  // create-new: once succeeds, again fails with EEXIST
  err = zsv_create_new_file(fresh);
  CHECK(err == 0, "create new file (len %zu): %s", len, strerror(err));
  CHECK(file_is(fresh, "") == 1, "created file missing or not empty (len %zu)", len);
  err = zsv_create_new_file(fresh);
  CHECK(err == EEXIST, "create existing file (len %zu): want EEXIST, got %d (%s)", len, err, strerror(err));

  err = zsv_copy_permissions(dst, fresh);
  CHECK(err == 0, "copy permissions (len %zu): %s", len, strerror(err));

  CHECK(zsv_same_file(dst, dst) == 1, "same file not recognized (len %zu)", len);
  CHECK(zsv_same_file(dst, fresh) == 0, "different files reported the same (len %zu)", len);

  zsv_remove(dst);
  zsv_remove(fresh);
}

// src and dst are long; files also exist at the first TEST_TRUNCATED_LEN bytes of each.
// Replacing src with dst must move the long-named files and leave the short-named ones
// alone; acting on truncated paths would overwrite the file at dst's truncated name
static void check_truncation(const char *dir) {
  char src[TEST_PATH_MAX], dst[TEST_PATH_MAX], src_cut[TEST_PATH_MAX], dst_cut[TEST_PATH_MAX];
  make_name(src, sizeof(src), dir, 's', TEST_LONG_FILE_LEN, ".csv");
  make_name(dst, sizeof(dst), dir, 'd', TEST_LONG_FILE_LEN, ".csv");
  if (strlen(dir) + 2 >= TEST_TRUNCATED_LEN) { // the cut would fall in dir rather than the file names
    fprintf(stderr, "SKIP truncation check: %s is too long\n", dir);
    return;
  }
  FORMAT_PATH(src_cut, "%.*s", TEST_TRUNCATED_LEN, src);
  FORMAT_PATH(dst_cut, "%.*s", TEST_TRUNCATED_LEN, dst);

  CHECK(write_file(src, "long") == 0, "cannot write %s", src);
  CHECK(write_file(src_cut, "cut-src") == 0, "cannot write %s", src_cut);
  CHECK(write_file(dst_cut, "victim") == 0, "cannot write %s", dst_cut);
  int err = zsv_replace_file(src, dst);
  CHECK(err == 0, "replace long paths: %s", strerror(err));
  CHECK(file_is(dst, "long") == 1, "long destination content wrong");
  CHECK(file_is(dst_cut, "victim") == 1, "file at the truncated destination name was changed");
  CHECK(file_is(src_cut, "cut-src") == 1, "file at the truncated source name was moved");
  zsv_remove(dst);
  zsv_remove(src_cut);
  zsv_remove(dst_cut);
}

#ifdef _WIN32
// a file held open cannot be replaced, which zsv_replace_file() reports as EBUSY
static void check_busy(const char *dir) {
  char src[TEST_PATH_MAX], dst[TEST_PATH_MAX];
  FORMAT_PATH(src, "%s/busy-src.csv", dir);
  FORMAT_PATH(dst, "%s/busy-dst.csv", dir);
  CHECK(write_file(src, "s") == 0, "cannot write %s", src);
  FILE *held = zsv_fopen(src, "rb");
  CHECK(held != NULL, "cannot open %s", src);
  int err = zsv_replace_file(src, dst);
  CHECK(err == EBUSY, "replace of an open file: want EBUSY, got %d (%s)", err, strerror(err));
  if (held)
    fclose(held);
  zsv_remove(src);
  zsv_remove(dst);
}

// GetFullPathNameW turns a device name into \\.\NUL, which needs no \\?\ prefix; read
// as a UNC path it would become \\?\UNC\.\NUL, which Windows does not resolve
static void check_device(void) {
  wchar_t *w = NULL;
  DWORD rc = zsv_pathToPrefixedWidePath("NUL", &w);
  CHECK(rc == 0 && w && !wcscmp(w, L"\\\\.\\NUL"), "NUL converted to %ls (rc %lu)", w ? w : L"(null)", rc);
  free(w);
}

// the read-only attribute, Windows' only permission bit, follows the source
static void check_read_only(const char *dir) {
  char from[TEST_PATH_MAX], to[TEST_PATH_MAX];
  FORMAT_PATH(from, "%s/ro-from.csv", dir);
  FORMAT_PATH(to, "%s/ro-to.csv", dir);
  CHECK(write_file(from, "x") == 0 && write_file(to, "y") == 0, "cannot write %s or %s", from, to);
  CHECK(_chmod(from, _S_IREAD) == 0, "cannot make %s read-only", from);
  int err = zsv_copy_permissions(from, to);
  CHECK(err == 0, "copy read-only: %s", strerror(err));
  CHECK(_access(to, 2) != 0, "read-only attribute not copied");
  CHECK(_chmod(from, _S_IREAD | _S_IWRITE) == 0, "cannot make %s writable", from);
  err = zsv_copy_permissions(from, to);
  CHECK(err == 0, "copy writable: %s", strerror(err));
  CHECK(_access(to, 2) == 0, "read-only attribute not cleared");
  _chmod(to, _S_IREAD | _S_IWRITE);
  zsv_remove(from);
  zsv_remove(to);
}

// a name that is not valid UTF-8 is refused with EILSEQ rather than mapped to another path
static void check_invalid_utf8(const char *dir) {
  char good[TEST_PATH_MAX], bad[TEST_PATH_MAX];
  FORMAT_PATH(good, "%s/utf8-good.csv", dir);
  FORMAT_PATH(bad, "%s/utf8-bad-\xe9.csv", dir); // Latin-1 e-acute: a lone UTF-8 lead byte

  CHECK(write_file(good, "keep") == 0, "cannot write %s", good);
  int err = zsv_replace_file(bad, good);
  CHECK(err == EILSEQ, "non-UTF-8 source: want EILSEQ, got %d (%s)", err, strerror(err));
  err = zsv_replace_file(good, bad);
  CHECK(err == EILSEQ, "non-UTF-8 destination: want EILSEQ, got %d (%s)", err, strerror(err));
  CHECK(file_is(good, "keep") == 1, "source changed by a refused replace");
  err = zsv_create_new_file(bad);
  CHECK(err == EILSEQ, "non-UTF-8 create: want EILSEQ, got %d (%s)", err, strerror(err));
  err = zsv_copy_permissions(good, bad);
  CHECK(err == EILSEQ, "non-UTF-8 copy permissions: want EILSEQ, got %d (%s)", err, strerror(err));
  CHECK(zsv_same_file(good, bad) == -1, "non-UTF-8 same-file: want -1");
  err = zsv_remove(bad);
  CHECK(err == EILSEQ, "non-UTF-8 remove: want EILSEQ, got %d (%s)", err, strerror(err));
  CHECK(!zsv_dir_exists_winlp(bad), "non-UTF-8 name reported as an existing directory");
  zsv_remove(good);
}
#endif

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <tmp dir>\n", argv[0]);
    return 1;
  }
  // an absolute base, so that a path's length is the length Windows checks against
  // MAX_PATH: a short relative path can resolve to one past it, which zsv_fopen()
  // still sends to fopen()
  char *base = TEST_ABSPATH(argv[1]);
  CHECK(base != NULL, "cannot resolve %s: %s", argv[1], strerror(errno));
  if (!base)
    return failures;
  char dir[TEST_PATH_MAX], long_dir[TEST_PATH_MAX];
  FORMAT_PATH(dir, "%s/test_replace_file.d", base);
  free(base);
  make_name(long_dir, sizeof(long_dir), dir, 'D', TEST_LONG_DIR_LEN, "");
  CHECK(TEST_MKDIR(dir) == 0 || errno == EEXIST, "cannot create %s: %s", dir, strerror(errno));
  CHECK(TEST_MKDIR(long_dir) == 0 || errno == EEXIST, "cannot create %s: %s", long_dir, strerror(errno));
  if (failures)
    return failures;

  check_dir(dir, 8);
  check_dir(long_dir, TEST_LONG_FILE_LEN);
  check_truncation(long_dir);
#ifdef _WIN32
  check_device();
  check_busy(dir);
  check_read_only(dir);
  check_invalid_utf8(dir);
#endif

  CHECK(TEST_RMDIR(long_dir) == 0, "cannot remove %s (files left behind?)", long_dir);
  CHECK(TEST_RMDIR(dir) == 0, "cannot remove %s (files left behind?)", dir);
  return failures;
}
