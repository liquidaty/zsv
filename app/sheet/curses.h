#if defined(WIN32) || defined(_WIN32)
#ifdef HAVE_NCURSESW
#include <ncursesw/ncurses.h>
#else
#include <ncurses/ncurses.h>
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
