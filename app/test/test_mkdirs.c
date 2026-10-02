// Unit test for zsv_mkdirs(): each new folder gets mode 0777 less the process umask.
// Exit status is the number of failed checks.
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zsv/utils/dirs.h>

#define FULL_DIR_MODE 0777

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

static void check_mode(const char *path, mode_t want) {
  struct stat st;
  if (stat(path, &st)) {
    CHECK(0, "stat(%s) failed", path);
    return;
  }
  mode_t got = st.st_mode & FULL_DIR_MODE;
  CHECK(got == want, "%s: mode %03o, expected %03o", path, (unsigned)got, (unsigned)want);
}

// Create <root>/<tag>/b/c under `mask`, as a dir path and as a file path, and check
// each folder it made
static void check_umask(const char *root, const char *tag, mode_t mask) {
  char a[FILENAME_MAX], b[FILENAME_MAX], c[FILENAME_MAX], f[FILENAME_MAX];
  mode_t want = FULL_DIR_MODE & ~mask;
  snprintf(a, sizeof(a), "%s/%s", root, tag);
  snprintf(b, sizeof(b), "%s/b", a);
  snprintf(c, sizeof(c), "%s/c", b);
  snprintf(f, sizeof(f), "%s/d/file.txt", c);

  mode_t old = umask(mask);
  CHECK(zsv_mkdirs(c, 0) == 0, "zsv_mkdirs(%s, 0) failed", c);
  CHECK(zsv_mkdirs(f, 1) == 0, "zsv_mkdirs(%s, 1) failed", f);
  umask(old);

  check_mode(a, want);
  check_mode(b, want);
  check_mode(c, want);
  snprintf(f, sizeof(f), "%s/d", c);
  check_mode(f, want);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <empty scratch dir>\n", argv[0]);
    return 1;
  }
  check_umask(argv[1], "u022", 022);
  check_umask(argv[1], "u007", 007);
  check_umask(argv[1], "u077", 077);
  check_umask(argv[1], "u000", 000);
  return failures;
}
