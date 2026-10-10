#ifndef ZSVSHEET_TERMINAL_H
#define ZSVSHEET_TERMINAL_H

// Terminal settings and output beyond what curses sets

#include "curses.h"

#include <limits.h>
#include <stddef.h>

#if defined(WIN32) || defined(_WIN32)
#include <windows.h>
#include <wchar.h>
#else
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

// Wide characters the UTF-8 print below converts at most: more than any row of text this app
// draws, and small enough for the stack
#define ZSVSHEET_UTF8_PRINT_WCHARS 1024

// Print len bytes of UTF-8 text at (row, col). A Win32 terminal renders text through wide
// characters; elsewhere curses reads UTF-8 directly. Not mvprintw: ncurses 6.3 formats text
// longer than the screen from a spent va_list. Text too long for the conversion is cut at a
// character boundary, as the screen edge cuts it
static inline void zsvsheet_mvaddnstr_utf8(int row, int col, const char *text, size_t len) {
  if (len > INT_MAX) // the casts below take a count the curses calls can hold
    len = INT_MAX;
#if defined(WIN32) || defined(_WIN32)
  wchar_t wbuf[ZSVSHEET_UTF8_PRINT_WCHARS];
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, wbuf, ZSVSHEET_UTF8_PRINT_WCHARS);
  if (!wlen && len > ZSVSHEET_UTF8_PRINT_WCHARS) {
    len = ZSVSHEET_UTF8_PRINT_WCHARS; // a conversion never needs more wide characters than bytes
    while (len && ((unsigned char)text[len] & 0xc0) == 0x80)
      len--; // do not cut a character in half
    wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, wbuf, ZSVSHEET_UTF8_PRINT_WCHARS);
  }
  if (wlen > 0)
    mvaddnwstr(row, col, wbuf, wlen);
#else
  mvaddnstr(row, col, text, (int)len);
#endif
}

// zsvsheet_keys_reach_app(): make <ctrl>c, <ctrl>s, <ctrl>q and <ctrl>v keys the app reads,
// rather than an interrupt, flow control or the terminal's literal-next. Other keys keep
// their meaning, so <ctrl>z still suspends and <ctrl>\ still quits at once (restoring the
// terminal first, as curses does for other signals that end the program). Call after curses'
// own terminal settings (cbreak/halfdelay). Returns 0, or -1 if the terminal could not be set
#if defined(WIN32) || defined(_WIN32)

static inline int zsvsheet_keys_reach_app(void) {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &mode))
    return -1;
  // <ctrl>c; the console has no flow control. PDCurses' raw() would also stop Enter arriving as '\n'
  return SetConsoleMode(in, mode & ~(DWORD)ENABLE_PROCESSED_INPUT) ? 0 : -1;
}
#else

static void zsvsheet_on_quit_signal(int sig) {
  endwin(); // leave the terminal usable, then end as the signal would have
  signal(sig, SIG_DFL);
  raise(sig);
}

static inline int zsvsheet_keys_reach_app(void) {
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &t))
    return -1;
  t.c_iflag &= ~(tcflag_t)IXON;    // <ctrl>s, <ctrl>q
  t.c_lflag &= ~(tcflag_t)IEXTEN;  // <ctrl>v
  t.c_cc[VINTR] = _POSIX_VDISABLE; // <ctrl>c
  if (tcsetattr(STDIN_FILENO, TCSANOW, &t))
    return -1;
  def_prog_mode(); // any later restore of the saved mode by curses keeps these settings
  void (*prior)(int) = signal(SIGQUIT, zsvsheet_on_quit_signal);
  if (prior == SIG_ERR)
    return -1;
  if (prior == SIG_IGN) // a parent that ignores it keeps doing so
    signal(SIGQUIT, SIG_IGN);
  return 0;
}
#endif

#endif // ZSVSHEET_TERMINAL_H
