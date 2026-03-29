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
