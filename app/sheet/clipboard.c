// System clipboard: see clipboard.h

#include "clipboard.h"

#if defined(WIN32) || defined(_WIN32)

#include <limits.h> // INT_MAX
#include <stdint.h> // SIZE_MAX
#include <windows.h>

enum zsvsheet_clipboard_status zsvsheet_clipboard_put(const char *text, size_t len) {
  enum zsvsheet_clipboard_status rc = zsvsheet_clipboard_error;
  HGLOBAL block = NULL;
  wchar_t *wtext;
  int wlen = 0;
  int converted = 0;

  if (len) {
    if (len > INT_MAX) // what MultiByteToWideChar() takes
      return zsvsheet_clipboard_error;
    wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, NULL, 0);
    if (wlen <= 0)
      return zsvsheet_clipboard_error;
  }
  // the wide string and its terminator; a size that would not fit is refused rather than wrapped
  if ((unsigned long long)wlen + 1 > (unsigned long long)SIZE_MAX / sizeof(wchar_t))
    return zsvsheet_clipboard_error;
  block = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)wlen + 1) * sizeof(wchar_t));
  if (!block)
    return zsvsheet_clipboard_error;
  wtext = (wchar_t *)GlobalLock(block);
  if (wtext) {
    if (wlen) {
      converted = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, wtext, wlen);
      if (converted == wlen)
        wtext[wlen] = L'\0';
    } else
      wtext[0] = L'\0'; // an empty value: an empty clipboard string, as on the other platforms
    GlobalUnlock(block);
  }
  if (converted != wlen)
    goto out;

  for (int tries = 0; tries < 5; tries++) {
    if (!OpenClipboard(NULL)) {
      Sleep(10); // another process holds the clipboard; it is usually gone in a moment
      continue;
    }
    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, block)) {
      block = NULL; // the system owns the block from here on
      rc = zsvsheet_clipboard_ok;
    }
    CloseClipboard();
    break;
  }

out:
  if (block)
    GlobalFree(block);
  return rc;
}

#else // POSIX: hand the text to a clipboard tool

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <zsv/utils/os.h> // zsv_now_ms()

// How long the clipboard tools together may take to accept the text and exit. The editor must
// keep responding: the main loop is the only one that reads keys, and <ctrl>c does not interrupt
// it. A tool that has not taken the text by now is not going to: pbcopy and its kin answer in
// milliseconds. One budget covers every candidate and both of the waits below
#define ZSVSHEET_CLIPBOARD_TIMEOUT_MS 500
#define ZSVSHEET_CLIPBOARD_SLICE_MS 10

#if defined(__APPLE__)
static const char *const clipboard_helper_pbcopy[] = {"pbcopy", NULL};
static const char *const *const clipboard_helpers[] = {clipboard_helper_pbcopy, NULL};
#else
// Wayland first, then X11; a host usually has one of them. A tool that is not installed exits
// 127 (see run_helper()), which moves on to the next; so does a tool that is installed and
// fails, since the right one for the session may be the next (wl-copy without a Wayland
// display, say)
static const char *const clipboard_helper_wl_copy[] = {"wl-copy", NULL};
static const char *const clipboard_helper_xclip[] = {"xclip", "-selection", "clipboard", NULL};
static const char *const clipboard_helper_xsel[] = {"xsel", "--clipboard", "--input", NULL};
static const char *const *const clipboard_helpers[] = {clipboard_helper_wl_copy, clipboard_helper_xclip,
                                                       clipboard_helper_xsel, NULL};
#endif

// Whether the tools have had their time: the clock decides, and the waits taken are the bound
// when the clock is unavailable (zsv_now_ms() returns 0)
static int clipboard_out_of_time(unsigned long long start_ms, unsigned waited_ms) {
  unsigned long long now = zsv_now_ms();
  if (start_ms && now)
    return now - start_ms >= ZSVSHEET_CLIPBOARD_TIMEOUT_MS;
  return waited_ms >= ZSVSHEET_CLIPBOARD_TIMEOUT_MS;
}

// Write all of text to fd, adding each wait to *waited_ms and giving up once the budget is
// spent (the tool may not be reading). Returns 0 once every byte is written
static int clipboard_write_all(int fd, const char *text, size_t len, unsigned long long start_ms, unsigned *waited_ms) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, text + off, len - off);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd;
      if (clipboard_out_of_time(start_ms, *waited_ms))
        return -1;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      // a ready poll (writable, or POLLERR/POLLHUP, whose write fails with EPIPE below) went
      // through no wait, so only a timed-out one spends the budget
      if (poll(&pfd, 1, ZSVSHEET_CLIPBOARD_SLICE_MS) == 0)
        *waited_ms += ZSVSHEET_CLIPBOARD_SLICE_MS;
      else if (pfd.revents == 0 && errno != EINTR)
        return -1;
      continue;
    }
    return -1; // EPIPE: the tool exited without reading
  }
  return 0;
}

// Run argv with text on its stdin, and reap it within what is left of *waited_ms' budget.
// Returns 0 when the tool took the text and exited 0, 1 when it is not installed, -1 on any
// other failure.
// Between fork() and exec() the child calls close, dup2, open and _exit, all async-signal-safe,
// and execvp, which POSIX does not list: this process has the index worker thread running, so
// a child that stalls there is possible, and the budget below is what bounds it
static int run_helper(const char *const *argv, const char *text, size_t len, unsigned long long start_ms,
                      unsigned *waited_ms) {
  int fds[2];
  pid_t pid;
  int rc, wrote;
  int flags;
  struct sigaction ignore, prev;
  int sigpipe_ignored = 0;

  if (pipe(fds))
    return -1;
  pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) { // child: stdin is the pipe, output goes nowhere
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO)
        close(devnull);
    }
    // dup2 first, then both of the pipe's own descriptors away: an inherited write end would
    // keep the tool from ever seeing end-of-input
    if (dup2(fds[0], STDIN_FILENO) < 0)
      _exit(127);
    close(fds[0]);
    close(fds[1]);
    execvp(argv[0], (char *const *)argv);
    _exit(127); // not installed
  }
  close(fds[0]);

  // the write must not block: the tool may be slow to read
  flags = fcntl(fds[1], F_GETFL, 0);
  wrote = -1;
  if (flags >= 0 && fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) == 0) {
    // SIGPIPE is ignored here in the parent, not before the fork, so the tool starts with the
    // disposition its parent had: a tool that exits without reading would otherwise end the
    // editor. The window is process-wide, so it is as short as the write
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, NULL, &prev) == 0 && sigaction(SIGPIPE, &ignore, NULL) == 0)
      sigpipe_ignored = 1;
    wrote = clipboard_write_all(fds[1], text, len, start_ms, waited_ms);
    if (sigpipe_ignored)
      sigaction(SIGPIPE, &prev, NULL);
  }
  close(fds[1]);

  rc = -1;
  while (1) {
    int wstatus = 0;
    pid_t r = waitpid(pid, &wstatus, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(wstatus)) {
        int code = WEXITSTATUS(wstatus);
        rc = code == 0 ? 0 : code == 127 ? 1 : -1;
      }
      break;
    }
    if (r < 0 && errno != EINTR) // ECHILD: already reaped, so there is nothing to wait for
      break;
    if (clipboard_out_of_time(start_ms, *waited_ms)) {
      kill(pid, SIGKILL);
      waitpid(pid, &wstatus, 0); // it dies at once; reap it here so no zombie is left behind
      break;
    }
    struct timespec slice;
    slice.tv_sec = 0;
    slice.tv_nsec = ZSVSHEET_CLIPBOARD_SLICE_MS * 1000000L;
    nanosleep(&slice, NULL);
    *waited_ms += ZSVSHEET_CLIPBOARD_SLICE_MS;
  }
  if (rc == 1)
    return 1; // not installed, whatever became of the write
  return wrote ? -1 : rc;
}

enum zsvsheet_clipboard_status zsvsheet_clipboard_put(const char *text, size_t len) {
  enum zsvsheet_clipboard_status rc = zsvsheet_clipboard_no_tool;
  unsigned long long start_ms = zsv_now_ms(); // 0: the waits taken are the bound instead
  unsigned waited_ms = 0;                     // and the tools share one budget

  for (size_t i = 0; clipboard_helpers[i]; i++) {
    int r;
    if (clipboard_out_of_time(start_ms, waited_ms))
      break;
    r = run_helper(clipboard_helpers[i], text, len, start_ms, &waited_ms);
    if (r == 0)
      return zsvsheet_clipboard_ok;
    if (r < 0)
      rc = zsvsheet_clipboard_error;
  }
  return rc;
}

#endif
