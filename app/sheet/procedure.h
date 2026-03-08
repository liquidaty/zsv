#ifndef ZSVSHEET_PROCEDURE_H
#define ZSVSHEET_PROCEDURE_H
#include <stdbool.h>
#include <zsv/ext.h>

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
  zsvsheet_builtin_proc_filter_this,
  zsvsheet_builtin_proc_sqlfilter,
  zsvsheet_builtin_proc_find,
  zsvsheet_builtin_proc_find_next,
  zsvsheet_builtin_proc_goto_column,
  zsvsheet_builtin_proc_open_file,
  zsvsheet_builtin_proc_resize,
  zsvsheet_builtin_proc_prompt,
  zsvsheet_builtin_proc_subcommand,
  zsvsheet_builtin_proc_help,
  zsvsheet_builtin_proc_vim_g_key_binding_dmux,
  zsvsheet_builtin_proc_newline,
  zsvsheet_builtin_proc_pivot_expr,
  zsvsheet_builtin_proc_pivot_cur_col,
  zsvsheet_builtin_proc_errors,
  zsvsheet_builtin_proc_errors_clear
};

#define ZSVSHEET_PROC_INVALID 0
#define ZSVSHEET_PROC_MAX_ARGS 5

/* Procedures perform various actions in the editor. Procedures can be invoked
 * by key-bindings, prompt invocation, another procedure or script. */

enum zsvsheet_proc_invocation_type {
  zsvsheet_proc_invocation_type_keypress,
  zsvsheet_proc_invocation_type_proc,
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
      struct {
        zsvsheet_proc_id_t id;
      } proc;
    } u;
  } invocation;

  int num_params;
  struct {
    /* TODO: typing */
    union {
      const char *string;
    } u;
  } params[ZSVSHEET_PROC_MAX_ARGS];

  /* Within context of which subcommand has this proc been called? */
  void *subcommand_context;
};

enum zsvsheet_proc_arg_type {
  ZSVSHEET_PROC_ARG_TYPE_INVALID,
  ZSVSHEET_PROC_ARG_TYPE_INT,
  ZSVSHEET_PROC_ARG_TYPE_STRING,
};

typedef zsvsheet_status (*zsvsheet_proc_fn)(struct zsvsheet_proc_context *ctx);

/* Wrapper for procedure invocation from keypress */
zsvsheet_status zsvsheet_proc_invoke_from_keypress(zsvsheet_proc_id_t proc_id, int ch, void *subcommand_context);

/* Invoke a procedure based on subcommand/script statement. For script support
 * this is likely too simple but its a nice simple API subcommand.  */
zsvsheet_status zsvsheet_proc_invoke_from_command(const char *command, struct zsvsheet_proc_context *context);

/* Base proc invocation function */
zsvsheet_status zsvsheet_proc_invoke(zsvsheet_proc_id_t proc_id, struct zsvsheet_proc_context *ctx);

/* Register builtin procedure with fixed id */
zsvsheet_proc_id_t zsvsheet_register_builtin_proc(zsvsheet_proc_id_t id, const char *name, const char *description,
                                                  zsvsheet_proc_fn handler);

/* Dynamically register a procedure, returns a positive id or negative error */
zsvsheet_proc_id_t zsvsheet_register_proc(const char *name, const char *description, zsvsheet_proc_fn handler);

/* Find procedure by name */
struct zsvsheet_procedure *zsvsheet_find_procedure_by_name(const char *name);

#endif /* ZSVSHEET_PROCEDURE_H */
