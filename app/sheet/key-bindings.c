#include "key-bindings.h"

#ifndef ZSVSHEET_CTRL
/* clang-format off */
#define ZSVSHEET_CTRL(c) ((c)&037)
/* clang-format on */
#endif

// to do: add customizable bindings
/* clang-format off */
static int zsvsheet_key_bindings[] = {
  zsvsheet_key_escape, 27, // escape
  zsvsheet_key_open_file, 'e',
  zsvsheet_key_move_bottom, KEY_SF, // shift + down
  zsvsheet_key_move_top, KEY_SR, // shift + up
  zsvsheet_key_find_next, 'n',
  zsvsheet_key_move_first_col, KEY_SLEFT,
  zsvsheet_key_move_last_col, KEY_SRIGHT,
  zsvsheet_key_move_up, KEY_UP,
  zsvsheet_key_move_down, KEY_DOWN,
  zsvsheet_key_move_left, KEY_LEFT,
  zsvsheet_key_move_right, KEY_RIGHT,
  zsvsheet_key_pg_down, ZSVSHEET_CTRL('f'),
  zsvsheet_key_pg_up, ZSVSHEET_CTRL('b'),
  zsvsheet_key_filter, 'f',
  zsvsheet_key_find, '/',
  zsvsheet_key_quit, 'q',
  zsvsheet_key_resize, KEY_RESIZE,
  -1,
  -1
};
/* clang-format on */

static int zsvsheet_ch_from_zsvsheetch(int zch) {
  for (int i = 0; zsvsheet_key_bindings[i] != -1; i += 2)
    if (zsvsheet_key_bindings[i] == zch)
      return zsvsheet_key_bindings[i + 1];
  return -1;
}

static enum zsvsheet_key zsvsheet_key_binding(int ch) {
  for (int i = 0; zsvsheet_key_bindings[i] != -1; i += 2) {
    if (zsvsheet_key_bindings[i + 1] == ch)
      return zsvsheet_key_bindings[i];
  }
  return zsvsheet_key_unknown;
}
