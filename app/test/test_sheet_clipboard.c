/*
 * Unit test of the sheet system clipboard module (app/sheet/clipboard.c) on POSIX: what the
 * clipboard tool receives, which failures it reports, and that a tool which never exits cannot
 * hold the editor (the module's deadline ends it).
 *
 * argv[1] is a scratch directory. Each case puts a directory of fake clipboard tools on PATH;
 * the fake the module is expected to run writes its stdin to the file CLIP_FILE names.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "../sheet/clipboard.h"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

// The tool the module tries first on this platform
#if defined(__APPLE__)
#define TOOL_NAME "pbcopy"
#else
#define TOOL_NAME "wl-copy"
#endif

static const char *scratch;
static char bin_path[4096];
static char clip_path[4096];
static char tool_path[4096];

// Write script to <scratch>/bin/<name>
static int write_script(const char *name, const char *script) {
  FILE *f;
  snprintf(tool_path, sizeof(tool_path), "%s/%s", bin_path, name);
  if (!(f = fopen(tool_path, "wb")))
    return -1;
  fputs(script, f);
  fclose(f);
  return chmod(tool_path, 0700);
}

static int read_file(const char *path, char *buf, size_t bufsz, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t bytes = fread(buf, 1, bufsz, f);
  fclose(f);
  *len = bytes;
  return 0;
}

static unsigned long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)tv.tv_sec * 1000 + (unsigned long long)tv.tv_usec / 1000;
}

// PATH with only the fakes (or nothing at all), CLIP_FILE pointing at the scratch directory
static void use_fake_path(void) {
  setenv("PATH", bin_path, 1);
  setenv("CLIP_FILE", clip_path, 1);
  remove(clip_path);
}

int main(int argc, const char *argv[]) {
  static char buf[256 * 1024];
  size_t len;
  unsigned long long started, elapsed;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]);
    return 1;
  }
  scratch = argv[1];
  mkdir(scratch, 0700); // the harness makes its parent; the failure is reported below
  snprintf(clip_path, sizeof(clip_path), "%s/clip.txt", scratch);
  snprintf(bin_path, sizeof(bin_path), "%s/bin", scratch);
  if (mkdir(bin_path, 0700) && errno != EEXIST) {
    fprintf(stderr, "%s: cannot create %s\n", argv[0], bin_path);
    return 1;
  }

  // no tool at all: an empty directory on PATH holds nothing to run
  static char empty_path[4096];
  snprintf(empty_path, sizeof(empty_path), "%s/empty", scratch);
  mkdir(empty_path, 0700);
  setenv("PATH", empty_path, 1);
  CHECK(zsvsheet_clipboard_put("x", 1) == zsvsheet_clipboard_no_tool);

  // the first tool the platform uses takes the text: what it receives is what was copied,
  // byte for byte, for a plain string, for UTF-8 with a line break, and for a payload larger
  // than a pipe buffer (so the write's poll loop is exercised too)
  // the script uses /bin paths: the case below runs with nothing but the fakes on PATH
  CHECK(!write_script(TOOL_NAME, "#!/bin/sh\n/bin/cat > \"$CLIP_FILE\"\n"));
  use_fake_path();
  CHECK(zsvsheet_clipboard_put("hello", 5) == zsvsheet_clipboard_ok);
  CHECK(!read_file(clip_path, buf, sizeof(buf), &len) && len == 5 && !memcmp(buf, "hello", 5));

  CHECK(zsvsheet_clipboard_put("a\xc3\xa9\nb", 5) == zsvsheet_clipboard_ok);
  CHECK(!read_file(clip_path, buf, sizeof(buf), &len) && len == 5 && !memcmp(buf, "a\xc3\xa9\nb", 5));

  for (size_t i = 0; i < sizeof(buf); i++)
    buf[i] = (char)('a' + i % 26);
  CHECK(zsvsheet_clipboard_put(buf, sizeof(buf)) == zsvsheet_clipboard_ok);
  static char back[256 * 1024];
  CHECK(!read_file(clip_path, back, sizeof(back), &len) && len == sizeof(buf) && !memcmp(back, buf, len));

  // a tool that is there and fails is an error, not "no tool"
  CHECK(!write_script(TOOL_NAME, "#!/bin/sh\n/bin/cat > /dev/null\nexit 3\n"));
  use_fake_path();
  CHECK(zsvsheet_clipboard_put("x", 1) == zsvsheet_clipboard_error);

  // a tool that reads nothing and exits at once: the write fails (SIGPIPE is ignored for it),
  // and the editor lives on
  CHECK(!write_script(TOOL_NAME, "#!/bin/sh\nexit 0\n"));
  use_fake_path();
  CHECK(zsvsheet_clipboard_put(buf, sizeof(buf)) == zsvsheet_clipboard_error);

  // a tool that is not installed (127) is "no tool", whatever became of the write
  CHECK(!write_script(TOOL_NAME, "#!/bin/sh\nexit 127\n"));
  use_fake_path();
  CHECK(zsvsheet_clipboard_put("x", 1) == zsvsheet_clipboard_no_tool);

  // a tool that never reads and never exits cannot hold the editor: the module's deadline
  // kills it, and the call still returns
  CHECK(!write_script(TOOL_NAME, "#!/bin/sh\nexec /bin/sleep 60\n"));
  use_fake_path();
  started = now_ms();
  CHECK(zsvsheet_clipboard_put("x", 1) == zsvsheet_clipboard_error);
  elapsed = now_ms() - started;
  CHECK(elapsed < 1500); // the budget is 500 ms; this only has to fail when it is not enforced

  return 0;
}
