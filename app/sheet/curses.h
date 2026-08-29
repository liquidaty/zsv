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

// How getch() delivers a typed non-ASCII character: ncurses returns its UTF-8
// bytes one call at a time; PDCurses (wincon/pdckbd.c) returns a UTF-16 code
// unit, which no caller here decodes yet, so such input is dropped there
#if defined(HAVE_PDCURSES) && !defined(HAVE_NCURSESW) && !defined(HAVE_NCURSES)
#define ZSVSHEET_GETCH_UTF8_BYTES 0
#else
#define ZSVSHEET_GETCH_UTF8_BYTES 1
#endif
