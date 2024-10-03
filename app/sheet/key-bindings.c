#include "key-bindings.h"

#ifndef ZTV_CTRL
#define ZTV_CTRL(c) ((c) & 037)
#endif

// to do: support customizable bindings
static enum ztv_key ztv_key_binding(int ch) {
  if(ch == 27) // escape
    return ztv_key_escape;
  if(ch == KEY_SF) // shift + down
    return ztv_key_move_bottom; 
  if(ch == KEY_SR) // shift + up
    return ztv_key_move_top;
  if(ch == 'n') return ztv_key_find_next;
  if(ch == KEY_SLEFT) return ztv_key_move_first_col;
  if(ch == KEY_SRIGHT) return ztv_key_move_last_col;
  if(ch == KEY_UP) return ztv_key_move_up;
  if(ch == KEY_DOWN) return ztv_key_move_down;
  if(ch == KEY_LEFT) return ztv_key_move_left;
  if(ch == KEY_RIGHT) return ztv_key_move_right;
  if(ch == ZTV_CTRL('f')) return ztv_key_pg_down;
  if(ch == ZTV_CTRL('b')) return ztv_key_pg_up;
  if(ch == 'f') return ztv_key_filter;
  if(ch == '/') return ztv_key_find;
  if(ch == 'q') return ztv_key_quit;
  return ztv_key_unknown;
}
