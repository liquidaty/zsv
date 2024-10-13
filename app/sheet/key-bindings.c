#include "key-bindings.h"

#ifndef ZSVSHEET_CTRL
#define ZSVSHEET_CTRL(c) ((c) & 037)
#endif

// to do: support customizable bindings
static enum zsvsheet_key zsvsheet_key_binding(int ch) {
  if (ch == 27) // escape
    return zsvsheet_key_escape;
  if (ch == 'e') // edit / open
    return zsvsheet_key_open_file;
  if (ch == KEY_SF) // shift + down
    return zsvsheet_key_move_bottom;
  if (ch == KEY_SR) // shift + up
    return zsvsheet_key_move_top;
  if (ch == 'n')
    return zsvsheet_key_find_next;
  if (ch == KEY_SLEFT)
    return zsvsheet_key_move_first_col;
  if (ch == KEY_SRIGHT)
    return zsvsheet_key_move_last_col;
  if (ch == KEY_UP)
    return zsvsheet_key_move_up;
  if (ch == KEY_DOWN)
    return zsvsheet_key_move_down;
  if (ch == KEY_LEFT)
    return zsvsheet_key_move_left;
  if (ch == KEY_RIGHT)
    return zsvsheet_key_move_right;
  if (ch == ZSVSHEET_CTRL('f'))
    return zsvsheet_key_pg_down;
  if (ch == ZSVSHEET_CTRL('b'))
    return zsvsheet_key_pg_up;
  if (ch == 'f')
    return zsvsheet_key_filter;
  if (ch == '/')
    return zsvsheet_key_find;
  if (ch == 'q')
    return zsvsheet_key_quit;
  if (ch == KEY_RESIZE)
    return zsvsheet_key_resize;
  return zsvsheet_key_unknown;
}
