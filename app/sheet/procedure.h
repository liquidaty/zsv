#ifndef ZSVSHEET_PROCEDURE_H
#define ZSVSHEET_PROCEDURE_H
#include <stdbool.h>

/* ID's of bulitin procedures, extensions can register more.
 *
 * TODO: What specific procedures are bulitin and what are their
 *       id's is not a concern of the procedure system. This should
 *       be defined by the registrar who defines those procedures,
 *       in this case sheet. move it out of here at some point.
 */
enum {
  zsvsheet_builtin_proc_unknown = 0,
  zsvsheet_builtin_proc_quit,
  zsvsheet_builtin_proc_escape,
  zsvsheet_builtin_proc_move_bottom,
  zsvsheet_builtin_proc_move_top,
  zsvsheet_builtin_proc_move_first_col,
  zsvsheet_builtin_proc_pg_down,
  zsvsheet_builtin_proc_pg_up,
  zsvsheet_builtin_proc_move_last_col,
  zsvsheet_builtin_proc_move_up,
  zsvsheet_builtin_proc_move_down,
  zsvsheet_builtin_proc_move_left,
  zsvsheet_builtin_proc_move_right,
  zsvsheet_builtin_proc_filter,
  zsvsheet_builtin_proc_find,
  zsvsheet_builtin_proc_find_next,
  zsvsheet_builtin_proc_open_file,
  zsvsheet_builtin_proc_resize,
  zsvsheet_builtin_proc_prompt,
};

#define ZSVSHEET_PROC_INVALID 0
typedef int zsvsheet_proc_id_t;


/* Procedures perform various actions in the editor. Procedures can be invoked
 * by key-bindings, prompt invocation, another procedure or script. */

enum zsvsheet_proc_invocation_type {
  zsvsheet_proc_invocation_type_keypress,
  /* Add more... */
};

struct zsvsheet_proc_context {
  zsvsheet_proc_id_t proc_id;
  /* Information about how this procedure was called i.e. through a keypress,
   * command invocation, another procedure, a script etc... */
  struct {
    enum zsvsheet_proc_invocation_type type;

    /* Can we prompt user to type-in parameters? If not prompt_get should fail. */
    bool interactive;

    /* Type specific info */
    union {
      struct {
        int ch;
      } keypress;
    } u;
  } invocation;

  /* Within context of which subcommand has this proc been called? */
  void *subcommand_context;
};

typedef zsvsheet_handler_status (*zsvsheet_proc_handler_fn)(struct zsvsheet_proc_context *ctx);

/* Wrapper for procedure invocation from keypress */
zsvsheet_handler_status zsvsheet_proc_invoke_from_keypress(zsvsheet_proc_id_t proc_id, int ch, void *subcommand_context);

/* Base proc invocation function */
zsvsheet_handler_status zsvsheet_proc_invoke(zsvsheet_proc_id_t proc_id, struct zsvsheet_proc_context *ctx);

/* Register builtin procedure with fixed id */
zsvsheet_proc_id_t zsvsheet_register_builtin_proc(zsvsheet_proc_id_t id, const char *name, zsvsheet_proc_handler_fn handler);

/* Dynamically register a procedure, returns a positive id or negative error */
zsvsheet_proc_id_t zsvsheet_register_proc(const char *name, zsvsheet_proc_handler_fn handler);

#endif /* ZSVSHEET_PROCEDURE_H */
