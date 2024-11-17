#include "procedure.h"
#include "key-bindings.h"

#include <errno.h>
#include <stdio.h>
#include <assert.h>

#ifndef ZSVSHEET_CTRL
/* clang-format off */
#define ZSVSHEET_CTRL(c) ((c)&037)
/* clang-format on */
#endif

#if 0
#define keyb_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define keyb_debug(...) ((void)0)
#endif

#define MAX_KEY_BINDINGS 512

static int prev_ch = -1;
static struct zsvsheet_key_binding key_bindings[MAX_KEY_BINDINGS] = {0};

zsvsheet_status zsvsheet_proc_key_binding_handler(struct zsvsheet_key_binding_context *ctx) {
  return zsvsheet_proc_invoke_from_keypress(ctx->binding->proc_id, ctx->ch, ctx->subcommand_context);
}

int zsvsheet_register_key_binding(struct zsvsheet_key_binding *binding) {
  if (zsvsheet_find_key_binding(binding->ch))
    return EEXIST; /* Key bound already */

  if (binding->proc_id != ZSVSHEET_PROC_INVALID && binding->handler == NULL)
    binding->handler = zsvsheet_proc_key_binding_handler;

  assert(binding->handler);

  for (int i = 0; i < MAX_KEY_BINDINGS; ++i) {
    if (key_bindings[i].ch == 0) {
      key_bindings[i] = *binding;
      return 0;
    }
  }

  return -ENOMEM;
}

int zsvsheet_register_proc_key_binding(char ch, zsvsheet_proc_id_t proc_id) {
  struct zsvsheet_key_binding binding = {
    .ch = ch,
    .proc_id = proc_id,
  };
  return zsvsheet_register_key_binding(&binding);
}

struct zsvsheet_key_binding *zsvsheet_get_key_binding(size_t i) {
  if (i >= MAX_KEY_BINDINGS)
    return NULL;

  if (key_bindings[i].ch != -1)
    return &key_bindings[i];

  return NULL;
}

struct zsvsheet_key_binding *zsvsheet_find_key_binding(int ch) {
  for (int i = 0; i < MAX_KEY_BINDINGS; ++i) {
    if (key_bindings[i].ch == ch)
      return &key_bindings[i];
  }
  return NULL;
}

/* Subcommand context, that we later pass to procedures, seems a little out
 * of place here. Alternatively this function could only return the binding
 * and allow user to execute it.
 */
zsvsheet_status zsvsheet_key_press(int ch, void *subcommand_context) {
  int ret = zsvsheet_status_error;
  struct zsvsheet_key_binding *binding;
  struct zsvsheet_key_binding_context ctx;

  keyb_debug("key press %d prev %d\n", ch, prev_ch);

  binding = zsvsheet_find_key_binding(ch);
  if (!binding)
    goto not_found;

  ctx.ch = ch;
  ctx.prev_ch = prev_ch;
  ctx.binding = binding;
  ctx.subcommand_context = subcommand_context;
  ret = binding->handler(&ctx);
not_found:
  prev_ch = ch;
  return ret;
}

/*
 * Specific key bindings
 */

void zsvsheet_register_builtin_key_bindings(struct zsvsheet_key_binding *arr) {
  for (struct zsvsheet_key_binding *binding = arr; binding->ch != -1; ++binding)
    zsvsheet_register_key_binding(binding);
}

/*
 * VIM key bindings
 */

zsvsheet_status zsvsheet_vim_g_key_binding_dmux_handler(struct zsvsheet_key_binding_context *ctx) {
  /* Double g takes you to the top of the file in vim. We need to wait for the
   * second g in a row to execute the procedure */
  assert(ctx->ch == 'g');
  if (ctx->ch == ctx->prev_ch)
    return zsvsheet_proc_invoke_from_keypress(zsvsheet_builtin_proc_move_top, ctx->ch, ctx->subcommand_context);
  return zsvsheet_status_ok;
}

/* clang-format off */
struct zsvsheet_key_binding zsvsheet_vim_key_bindings[] = {
  { .ch = 'q',                 .proc_id = zsvsheet_builtin_proc_quit,          },

  { .ch = 27,                  .proc_id = zsvsheet_builtin_proc_escape,        },
  {
    .ch = KEY_RESIZE,
    .proc_id = zsvsheet_builtin_proc_resize,
    .hidden = 1,
  },
  { .ch = '^',                 .proc_id = zsvsheet_builtin_proc_move_first_col,},
  { .ch = '$',                 .proc_id = zsvsheet_builtin_proc_move_last_col, },
  { .ch = KEY_SLEFT,           .proc_id = zsvsheet_builtin_proc_move_first_col,},
  { .ch = KEY_SRIGHT,          .proc_id = zsvsheet_builtin_proc_move_last_col, },

  { .ch = 'k',                 .proc_id = zsvsheet_builtin_proc_move_up,       },
  { .ch = 'j',                 .proc_id = zsvsheet_builtin_proc_move_down,     },
  { .ch = 'h',                 .proc_id = zsvsheet_builtin_proc_move_left,     },
  { .ch = 'l',                 .proc_id = zsvsheet_builtin_proc_move_right,    },
  { .ch = KEY_UP,              .proc_id = zsvsheet_builtin_proc_move_up,       },
  { .ch = KEY_DOWN,            .proc_id = zsvsheet_builtin_proc_move_down,     },
  { .ch = KEY_LEFT,            .proc_id = zsvsheet_builtin_proc_move_left,     },
  { .ch = KEY_RIGHT,           .proc_id = zsvsheet_builtin_proc_move_right,    },

  { .ch = ZSVSHEET_CTRL('d'),  .proc_id = zsvsheet_builtin_proc_pg_down,       },
  { .ch = ZSVSHEET_CTRL('u'),  .proc_id = zsvsheet_builtin_proc_pg_up,         },
  { .ch = KEY_NPAGE,           .proc_id = zsvsheet_builtin_proc_pg_down,       },
  { .ch = KEY_PPAGE,           .proc_id = zsvsheet_builtin_proc_pg_up,         },

  {
    .ch = 'g',
    .ch_name = "g g",
    .proc_id = zsvsheet_builtin_proc_move_top,
    .handler = zsvsheet_vim_g_key_binding_dmux_handler
  },
  { .ch = 'G',                 .proc_id = zsvsheet_builtin_proc_move_bottom,   },
  /* Shift up/down also move you to the top/bottom of the page but have a
   * slightly different behaviour in terms of what ends up in the view but 
   * I don't think it is relevant.
   *
   * In any case curses doesn't implement KEY_SUP and KEY_SDOWN.
   * TODO: Figure out how to map those keys.
   */
  // { .ch = KEY_SUP,             .proc_id = zsvsheet_builtin_proc_move_top,      },
  // { .ch = KEY_SDOWN,           .proc_id = zsvsheet_builtin_proc_move_bottom,   },

  { .ch = '/',                 .proc_id = zsvsheet_builtin_proc_find,          },
  { .ch = 'n',                 .proc_id = zsvsheet_builtin_proc_find_next,     },

  /* Open is a subcommand only in vim. Keeping the binding for now */
  { .ch = 'e',                 .proc_id = zsvsheet_builtin_proc_open_file,     },
  { .ch = 'f',                 .proc_id = zsvsheet_builtin_proc_filter,        },
  { .ch = '?',                 .proc_id = zsvsheet_builtin_proc_help,          },

  { .ch = -1                                                          }
};
/* clang-format on */

void zsvsheet_register_vim_key_bindings(void) {
  zsvsheet_register_builtin_key_bindings(zsvsheet_vim_key_bindings);
}

/*
 * EMACS key bindings
 */

zsvsheet_status zsvsheet_emacs_Cx_key_binding_dmux_handler(struct zsvsheet_key_binding_context *ctx) {
  if (ctx->ch == ZSVSHEET_CTRL('c'))
    return zsvsheet_proc_invoke_from_keypress(zsvsheet_builtin_proc_quit, ctx->ch, ctx->subcommand_context);
  return zsvsheet_status_ok;
}

zsvsheet_status zsvsheet_emacs_Cf_key_binding_dmux_handler(struct zsvsheet_key_binding_context *ctx) {
  if (ctx->prev_ch == ZSVSHEET_CTRL('x') && ctx->ch == ZSVSHEET_CTRL('f'))
    return zsvsheet_proc_invoke_from_keypress(zsvsheet_builtin_proc_open_file, ctx->ch, ctx->subcommand_context);
  return zsvsheet_status_ok;
}

zsvsheet_status zsvsheet_emacs_Cs_key_binding_dmux_handler(struct zsvsheet_key_binding_context *ctx) {
  (void)(ctx);
  return zsvsheet_status_ok;
}

/* clang-format off */
struct zsvsheet_key_binding zsvsheet_emacs_key_bindings[] = {
  { .ch = 27,                  .proc_id = zsvsheet_builtin_proc_escape,        },
  {
    .ch = KEY_RESIZE,
    .proc_id = zsvsheet_builtin_proc_resize,
    .hidden = 1,
  },
  { .ch = ZSVSHEET_CTRL('a'),  .proc_id = zsvsheet_builtin_proc_move_first_col,},
  { .ch = ZSVSHEET_CTRL('e'),  .proc_id = zsvsheet_builtin_proc_move_last_col,},

  { .ch = KEY_UP,              .proc_id = zsvsheet_builtin_proc_move_up,       },
  { .ch = KEY_DOWN,            .proc_id = zsvsheet_builtin_proc_move_down,     },
  { .ch = KEY_LEFT,            .proc_id = zsvsheet_builtin_proc_move_left,     },
  { .ch = KEY_RIGHT,           .proc_id = zsvsheet_builtin_proc_move_right,    },

  { .ch = ZSVSHEET_CTRL('v'),  .proc_id = zsvsheet_builtin_proc_pg_down,       },
  { .ch = ZSVSHEET_CTRL('u'),  .proc_id = zsvsheet_builtin_proc_pg_up,         },
  { .ch = KEY_NPAGE,           .proc_id = zsvsheet_builtin_proc_pg_down,       },
  { .ch = KEY_PPAGE,           .proc_id = zsvsheet_builtin_proc_pg_up,         },

  // TODO: find is ctrl-s in emacs, pressed again causes find next.
  //       However, it is being captured by the shell. Figure out
  //       how to prevent that, if emacs can we can do it too.
  //{ .ch = ZSVSHEET_CTRL('s'),     .handler = zsvsheet_emacs_Cs_key_binding_dmux_handler, },

  { .ch = ZSVSHEET_CTRL('f'),     .handler = zsvsheet_emacs_Cf_key_binding_dmux_handler,  },
  /* No such thing in emacs, find a more suitable binding */
  { .ch = 'f',                    .proc_id = zsvsheet_builtin_proc_filter,        },
  { .ch = ZSVSHEET_CTRL('h'),     .proc_id = zsvsheet_builtin_proc_help,          },


  { .ch = -1                                                          }
};
/* clang-format on */

void zsvsheet_register_emacs_key_bindings(void) {
  zsvsheet_register_builtin_key_bindings(zsvsheet_emacs_key_bindings);
}

struct zsvsheet_ch_name {
  int ch;
  const char *name;
};

/* clang-format off */
struct zsvsheet_ch_name zsvsheet_common_ch_names[] = {
  { .ch = KEY_RESIZE,         .name = "<resize>"        },
  { .ch = 27,                 .name = "<esc>"           },
  { .ch = KEY_SLEFT,          .name = "<shift><left>"   },
  { .ch = KEY_SRIGHT,         .name = "<shift><right>"  },
  { .ch = KEY_UP,             .name = "<up>"            },
  { .ch = KEY_DOWN,           .name = "<down>"          },
  { .ch = KEY_LEFT,           .name = "<left>"          },
  { .ch = KEY_RIGHT,          .name = "<right>"         },
  { .ch = ZSVSHEET_CTRL('a'), .name = "<ctrl>a"         },
  { .ch = ZSVSHEET_CTRL('d'), .name = "<ctrl>d"         },
  { .ch = ZSVSHEET_CTRL('e'), .name = "<ctrl>e"         },
  { .ch = ZSVSHEET_CTRL('f'), .name = "<ctrl>f"         },
  { .ch = ZSVSHEET_CTRL('h'), .name = "<ctrl>f"         },
  { .ch = ZSVSHEET_CTRL('u'), .name = "<ctrl>u"         },
  { .ch = ZSVSHEET_CTRL('v'), .name = "<ctrl>v"         },
  { .ch = KEY_NPAGE,          .name = "<page up>"       },
  { .ch = KEY_PPAGE,          .name = "<page down>"     },
  { .ch = -1                                            },
};
/* clang-format on */

const char *zsvsheet_key_binding_ch_name(struct zsvsheet_key_binding *binding) {
  if (binding->ch_name)
    return binding->ch_name;

  for (int i = 0; zsvsheet_common_ch_names[i].ch != -1; i++) {
    if (zsvsheet_common_ch_names[i].ch == binding->ch)
      return zsvsheet_common_ch_names[i].name;
  }

  return keyname(binding->ch);
}
