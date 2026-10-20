// Unit test for zsv_get_temp_filename() directory selection on POSIX: $TMPDIR, then
// ZSV_SYSTEM_TMPDIR (compiled in as a relative dir), then ".". Most cases fail a
// directory with ENOENT so that they also run as root. Exit status is the number of
// failed checks.
// Usage: test_temp_filename <absolute scratch dir>
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zsv/utils/file.h>

#ifndef ZSV_SYSTEM_TMPDIR
#error build with -DZSV_SYSTEM_TMPDIR='"<relative dir>"'
#endif
#define SYSTMP ZSV_SYSTEM_TMPDIR
#define PREFIX "zt"
#define DIR_MODE 0755
#define RO_DIR_MODE 0555
#define ERRBUF_SIZE 4096

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

static const char *root;
static char case_dir[FILENAME_MAX];
static char errtext[ERRBUF_SIZE]; // stderr printed by the last call

// buf = <dir>/<name>. Returns 0, or -1 (a failed check) if it does not fit
static int path_in(char *buf, size_t size, const char *dir, const char *name) {
  int n = snprintf(buf, size, "%s/%s", dir, name);
  if (n < 0 || (size_t)n >= size) {
    CHECK(0, "path too long: %s/%s", dir, name);
    return -1;
  }
  return 0;
}

static int starts_with(const char *s, const char *pfx) {
  return s && !strncmp(s, pfx, strlen(pfx));
}

// Make a fresh <root>/<name> and chdir into it. Returns 0 on success
static int enter_case(const char *name) {
  if (path_in(case_dir, sizeof(case_dir), root, name))
    return -1;
  if (mkdir(case_dir, DIR_MODE) || chdir(case_dir)) {
    CHECK(0, "%s: %s", case_dir, strerror(errno));
    return -1;
  }
  return 0;
}

// Run `body` in a fresh <root>/<name>, then return to root. A body returns early only after
// a failed check
static void run_case(const char *name, void (*body)(void)) {
  if (enter_case(name))
    return;
  body();
  if (chdir(root))
    CHECK(0, "chdir %s: %s", root, strerror(errno));
}

// mkdir in the current case. Returns 0, or -1 (a failed check)
static int make_dir(const char *name, mode_t mode) {
  if (mkdir(name, mode)) {
    CHECK(0, "mkdir %s: %s", name, strerror(errno));
    return -1;
  }
  return 0;
}

// Call zsv_get_temp_filename() with fd 2 sent to a file, or to `err_fd` if >= 0, and
// keep what was written in errtext. errno is as the call left it
static char *get_temp(int err_fd) {
  char errfn[FILENAME_MAX];
  if (path_in(errfn, sizeof(errfn), case_dir, "stderr.txt"))
    return NULL;
  *errtext = '\0';
  fflush(stderr);
  int saved = dup(2);
  int fd = err_fd >= 0 ? err_fd : open(errfn, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (saved < 0 || fd < 0 || dup2(fd, 2) < 0) {
    CHECK(0, "redirecting stderr: %s", strerror(errno));
    if (saved >= 0)
      close(saved);
    if (fd >= 0 && fd != err_fd)
      close(fd);
    return NULL;
  }
  char *fn = zsv_get_temp_filename(PREFIX);
  int e = errno;
  fflush(stderr);
  dup2(saved, 2);
  close(saved);
  if (err_fd < 0) {
    close(fd);
    FILE *f = fopen(errfn, "rb");
    if (f) {
      size_t n = fread(errtext, 1, sizeof(errtext) - 1, f);
      errtext[n] = '\0';
      fclose(f);
    }
    remove(errfn);
  }
  errno = e;
  return fn;
}

static void done_with(char *fn) {
  if (fn)
    remove(fn);
  free(fn);
}

// TMPDIR unset: the system dir is used, silently
static void case_unset(void) {
  if (make_dir(SYSTMP, DIR_MODE))
    return;
  unsetenv("TMPDIR");
  char *fn = get_temp(-1);
  CHECK(starts_with(fn, SYSTMP "/" PREFIX), "unset: got %s", fn ? fn : "NULL");
  CHECK(!*errtext, "unset: unexpected stderr: %s", errtext);
  done_with(fn);
}

// TMPDIR names a usable dir: it is used ahead of the system dir
static void case_tmpdir(void) {
  char want[FILENAME_MAX];
  if (make_dir(SYSTMP, DIR_MODE) || make_dir("t", DIR_MODE))
    return;
  if (path_in(want, sizeof(want), case_dir, "t"))
    return;
  setenv("TMPDIR", want, 1);
  strncat(want, "/" PREFIX, sizeof(want) - strlen(want) - 1);
  char *fn = get_temp(-1);
  CHECK(starts_with(fn, want), "tmpdir: got %s, expected %s...", fn ? fn : "NULL", want);
  CHECK(!*errtext, "tmpdir: unexpected stderr: %s", errtext);
  done_with(fn);
}

// TMPDIR is empty: treated as unset, so nothing fails
static void case_empty(void) {
  if (make_dir(SYSTMP, DIR_MODE))
    return;
  setenv("TMPDIR", "", 1);
  char *fn = get_temp(-1);
  CHECK(starts_with(fn, SYSTMP "/" PREFIX), "empty: got %s", fn ? fn : "NULL");
  CHECK(!*errtext, "empty: unexpected stderr: %s", errtext);
  done_with(fn);
}

// TMPDIR does not exist: falls back to the system dir, reporting both
static void case_missing_tmpdir(void) {
  char missing[FILENAME_MAX], msg[FILENAME_MAX + 64];
  if (make_dir(SYSTMP, DIR_MODE))
    return;
  if (path_in(missing, sizeof(missing), case_dir, "nonexistent"))
    return;
  setenv("TMPDIR", missing, 1);
  char *fn = get_temp(-1);
  CHECK(starts_with(fn, SYSTMP "/" PREFIX), "missing: got %s", fn ? fn : "NULL");
  snprintf(msg, sizeof(msg), "cannot create temporary file in %s: ", missing);
  CHECK(strstr(errtext, msg), "missing: stderr lacks '%s': %s", msg, errtext);
  CHECK(strstr(errtext, "Warning: using " SYSTMP " for temporary files"), "missing: no fallback warning: %s", errtext);
  done_with(fn);
}

// TMPDIR and the system dir do not exist: the current directory is used
static void case_cwd(void) {
  setenv("TMPDIR", "nonexistent", 1);
  char *fn = get_temp(-1);
  CHECK(starts_with(fn, "./" PREFIX), "cwd: got %s", fn ? fn : "NULL");
  CHECK(strstr(errtext, "Warning: using . for temporary files"), "cwd: no fallback warning: %s", errtext);
  done_with(fn);
}

// chdir into a dir and remove it, so "." fails with ENOENT. Returns 0 on success
static int enter_removed_dir(void) {
  char gone[FILENAME_MAX];
  if (path_in(gone, sizeof(gone), case_dir, "gone"))
    return -1;
  if (mkdir(gone, DIR_MODE) || chdir(gone) || rmdir(gone)) {
    CHECK(0, "%s: %s", gone, strerror(errno));
    return -1;
  }
  return 0;
}

// every candidate fails: NULL, errno from the last one, and the last dir is named
static void case_all_fail(void) {
  if (enter_removed_dir())
    return;
  setenv("TMPDIR", "nonexistent", 1);
  char *fn = get_temp(-1);
  int e = errno;
  CHECK(!fn, "allfail: got %s", fn);
  CHECK(e == ENOENT, "allfail: errno %d (%s), expected ENOENT", e, strerror(e));
  CHECK(strstr(errtext, "cannot create temporary file in .: "), "allfail: stderr lacks '.': %s", errtext);
  done_with(fn);
}

// as case_all_fail, but with stderr unwritable so the failure messages clobber errno
static void case_all_fail_errno(void) {
  int ro = open("/dev/null", O_RDONLY);
  if (ro < 0) {
    CHECK(0, "/dev/null: %s", strerror(errno));
    return;
  }
  // the check below needs a failed write to stderr to change errno
  fflush(stderr);
  int saved = dup(2);
  dup2(ro, 2);
  errno = 0;
  fputs("probe", stderr);
  fflush(stderr);
  int probe = errno;
  dup2(saved, 2);
  close(saved);
  if (!probe) {
    fprintf(stderr, "skip allfail-errno: a failed stderr write leaves errno unchanged here\n");
    close(ro);
    return;
  }
  if (enter_removed_dir()) {
    close(ro);
    return;
  }
  setenv("TMPDIR", "nonexistent", 1);
  char *fn = get_temp(ro);
  int e = errno;
  close(ro);
  CHECK(!fn, "allfail-errno: got %s", fn);
  CHECK(e == ENOENT, "allfail-errno: errno %d (%s), expected ENOENT", e, strerror(e));
  done_with(fn);
}

// a read-only TMPDIR is named with EACCES
static void case_readonly(void) {
  char ro[FILENAME_MAX], msg[FILENAME_MAX + 256];
  if (geteuid() == 0) {
    fprintf(stderr, "skip readonly: permissions do not apply to root\n");
    return;
  }
  if (make_dir("ro", RO_DIR_MODE))
    return;
  if (path_in(ro, sizeof(ro), case_dir, "ro"))
    return;
  if (enter_removed_dir())
    return;
  setenv("TMPDIR", ro, 1);
  char *fn = get_temp(-1);
  CHECK(!fn, "readonly: got %s", fn);
  snprintf(msg, sizeof(msg), "cannot create temporary file in %s: %s", ro, strerror(EACCES));
  CHECK(strstr(errtext, msg), "readonly: stderr lacks '%s': %s", msg, errtext);
  done_with(fn);
}

int main(int argc, char *argv[]) {
  if (argc != 2 || *argv[1] != '/') {
    fprintf(stderr, "Usage: %s <absolute scratch dir>\n", argv[0]);
    return 1;
  }
  root = argv[1];
  run_case("unset", case_unset);
  run_case("tmpdir", case_tmpdir);
  run_case("empty", case_empty);
  run_case("missing", case_missing_tmpdir);
  run_case("cwd", case_cwd);
  run_case("allfail", case_all_fail);
  run_case("allfail-errno", case_all_fail_errno);
  run_case("readonly", case_readonly);
  return failures;
}
