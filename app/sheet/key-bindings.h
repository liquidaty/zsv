#ifndef ZSVSHEET_KEY_BINDINGS_H
#define ZSVSHEET_KEY_BINDINGS_H
#include "procedure.h"

enum zsvsheet_key {
  zsvsheet_key_unknown = 0,
  zsvsheet_key_quit,
  zsvsheet_key_escape,
  zsvsheet_key_move_bottom,
  zsvsheet_key_move_top,
  zsvsheet_key_move_first_col,
  zsvsheet_key_pg_down,
  zsvsheet_key_pg_up,
  zsvsheet_key_move_last_col,
  zsvsheet_key_move_up,
  zsvsheet_key_move_down,
  zsvsheet_key_move_left,
  zsvsheet_key_move_right,
  zsvsheet_key_filter,
  zsvsheet_key_find,
  zsvsheet_key_find_next,
  zsvsheet_key_goto_column,
  zsvsheet_key_open_file,
  zsvsheet_key_resize,
  zsvsheet_key_question,
};

struct zsvsheet_key_binding_context;

typedef zsvsheet_status (*zsvsheet_key_binding_fn)(struct zsvsheet_key_binding_context *);

/* Most key bindings will simply execute a procedure, handler then becomes
 * a simple wrapper around proc_invoke. More sophisticated key mappings can
 * be implemented if need be by defining a custom handler */
struct zsvsheet_key_binding {
  int ch;
  const char *ch_name;
  char hidden;
  zsvsheet_key_binding_fn handler;
  zsvsheet_proc_id_t proc_id;
  unsigned char taken : 1;
  unsigned char _ : 7;
};

struct zsvsheet_key_binding_context {
  int ch;
  int prev_ch;
  const struct zsvsheet_key_binding *binding;
  void *subcommand_context;
};

/* Use this one if you just need to call a procedure */
int zsvsheet_register_proc_key_binding(char ch, zsvsheet_proc_id_t proc_id);

/* Use this one if you key binding is stateful and you need logic */
int zsvsheet_register_key_binding(struct zsvsheet_key_binding *binding);

zsvsheet_status zsvsheet_key_press(int ch, void *subcommand_context);

struct zsvsheet_key_binding *zsvsheet_get_key_binding(size_t i);
struct zsvsheet_key_binding *zsvsheet_find_key_binding(int ch);

/*
 * Specific key bindings
 */

void zsvsheet_register_vim_key_bindings(void);

void zsvsheet_register_emacs_key_bindings(void);

const char *zsvsheet_key_binding_ch_name(struct zsvsheet_key_binding *binding);

#endif
