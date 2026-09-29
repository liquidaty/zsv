#if defined(WIN32) || defined(_WIN32)
#ifdef HAVE_NCURSESW
#include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSES)
#include <ncurses/ncurses.h>
#elif defined(HAVE_PDCURSES)
#define PDC_WIDE
#define set_escdelay(x) // no-op for PDCurses
#include <curses.h>
#endif // HAVE_NCURSESW
#else
#if __has_include(<ncursesw/curses.h>)
#include <ncursesw/curses.h>
#elif __has_include(<curses.h>)
#include <curses.h>
#else
#error Cannot find ncurses include file!
#endif
#endif

// Differences between the curses backends:
// - ZSVSHEET_GETCH_UTF8_BYTES: how getch() delivers a typed non-ASCII character: ncurses
//   returns its UTF-8 bytes one call at a time; PDCurses (wincon/pdckbd.c) returns a UTF-16
//   code unit, which no caller here decodes yet, so such input is dropped there
// - ZSVSHEET_NO_FLOW_CONTROL(): let <ctrl>s and <ctrl>q reach the app. ncurses: raw() turns
//   off XON/XOFF; call it before halfdelay(), which (via cbreak()) turns signals such as
//   <ctrl>c back on. PDCurses: the Windows console has no XON/XOFF, and raw() there would
//   stop Enter arriving as '\n' and turn off <ctrl>c
// - ZSVSHEET_KEY_PAD_ENTER: the key code of keypad Enter: KEY_ENTER on ncurses (application
//   keypad mode), PADENTER on PDCurses (wincon/pdckbd.c)
#if defined(HAVE_PDCURSES) && !defined(HAVE_NCURSESW) && !defined(HAVE_NCURSES)
#define ZSVSHEET_GETCH_UTF8_BYTES 0
#define ZSVSHEET_NO_FLOW_CONTROL() ((void)0)
#define ZSVSHEET_KEY_PAD_ENTER PADENTER
#else
#define ZSVSHEET_GETCH_UTF8_BYTES 1
#define ZSVSHEET_NO_FLOW_CONTROL() raw()
#define ZSVSHEET_KEY_PAD_ENTER KEY_ENTER
#endif
