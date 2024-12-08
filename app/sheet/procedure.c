#include "procedure.h"

#if 0
#define proc_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define proc_debug(...) ((void)0)
#endif

#define MAX_PROCEDURES 512

/* Each procedure can be given a name and in the future argument specification
 * so it can be typed from the procedureline. Simple procedure like call motion
 * don't can omit a name meaning they can only be executed via key binding.
 */
struct zsvsheet_procedure {
  zsvsheet_proc_id_t id;
  const char *name;
  const char *description;
  zsvsheet_proc_fn handler;
};

/* This array both stores procedures and works as a lookup table. */
static struct zsvsheet_procedure procedure_lookup[MAX_PROCEDURES] = {0};

static inline bool is_valid_proc_id(zsvsheet_proc_id_t id) {
  return (0 < id && id < MAX_PROCEDURES);
}

struct zsvsheet_procedure *zsvsheet_find_procedure(zsvsheet_proc_id_t proc_id) {
  struct zsvsheet_procedure *proc;
  if (!is_valid_proc_id(proc_id))
    return NULL;
  proc = &procedure_lookup[proc_id];
  if (proc->id == ZSVSHEET_PROC_INVALID)
    return NULL;
  assert(proc->id == proc_id);
  return proc;
}

struct zsvsheet_procedure *zsvsheet_find_procedure_by_name(const char *name) {
  struct zsvsheet_procedure *proc;
  if (!name)
    return NULL;
  for (int i = 0; i < MAX_PROCEDURES; ++i) {
    proc = &procedure_lookup[i];
    if (is_valid_proc_id(proc->id) && proc->name && !strcmp(name, proc->name))
      return proc;
  }
  return NULL;
}

static zsvsheet_proc_id_t zsvsheet_generate_proc_id(void) {
  for (zsvsheet_proc_id_t id = MAX_PROCEDURES - 1; id > ZSVSHEET_PROC_INVALID; --id) {
    if (!is_valid_proc_id(procedure_lookup[id].id))
      return id;
  }
  return ZSVSHEET_PROC_INVALID;
}

zsvsheet_status zsvsheet_proc_invoke(zsvsheet_proc_id_t proc_id, struct zsvsheet_proc_context *ctx) {
  proc_debug("invoke proc %d\n", proc_id);
  struct zsvsheet_procedure *proc = zsvsheet_find_procedure(proc_id);
  if (proc) {
    proc_debug("call proc %d handler %p\n", proc->id, proc->handler);
    return proc->handler(ctx);
  }
  return -1;
}

zsvsheet_status zsvsheet_proc_invoke_from_keypress(zsvsheet_proc_id_t proc_id, int ch, void *subcommand_context) {
  struct zsvsheet_proc_context context = {
    .proc_id = proc_id,
    .invocation.type = zsvsheet_proc_invocation_type_keypress,
    .invocation.interactive = true,
    .invocation.u.keypress.ch = ch,
    .subcommand_context = subcommand_context,
  };
  return zsvsheet_proc_invoke(proc_id, &context);
}

zsvsheet_status zsvsheet_proc_invoke_from_command(const char *command, struct zsvsheet_proc_context *context) {
  char *toks[10] = {0};
  char tokbuf[1024];
  struct zsvsheet_lexer lexer;
  char *proc_name;
  struct zsvsheet_procedure *proc;

  proc_debug("invoke from command: %s\n", command);
  zsvsheet_lexer_init(&lexer, command, tokbuf, sizeof(tokbuf), toks, sizeof(toks) / sizeof(toks[0]));

  if (zsvsheet_lexer_parse(&lexer) != zsvsheet_lexer_status_ok)
    goto out;
  if (lexer.num_toks == 0 || lexer.num_toks - 1 > ZSVSHEET_PROC_MAX_ARGS)
    goto out;

  proc_name = lexer.toks[0];
  proc = zsvsheet_find_procedure_by_name(proc_name);
  if (!proc)
    goto out;

  /* Prototypes can be added to procedures to specify what exact arguments they
   * take. Here the arguments could be validated and typechecked against the
   * prototype. For now we just pass the parameters as they are */
  context->proc_id = proc->id;
  context->num_params = lexer.num_toks - 1;
  for (int i = 0; i < context->num_params; ++i)
    context->params[i].u.string = lexer.toks[i + 1];

  return zsvsheet_proc_invoke(proc->id, context);
out:
  return zsvsheet_status_error;
}

static zsvsheet_proc_id_t zsvsheet_do_register_proc(struct zsvsheet_procedure *proc) {
  proc_debug("register proc %d %s\n", proc->id, proc->name ? proc->name : "(unnamed)");
  if (!is_valid_proc_id(proc->id))
    return -1;
  if (zsvsheet_find_procedure(proc->id))
    return -1;
  procedure_lookup[proc->id] = *proc;
  return proc->id;
}

zsvsheet_proc_id_t zsvsheet_register_builtin_proc(zsvsheet_proc_id_t id, const char *name, const char *description,
                                                  zsvsheet_proc_fn handler) {
  struct zsvsheet_procedure procedure = {.id = id, .name = name, .description = description, .handler = handler};
  return zsvsheet_do_register_proc(&procedure);
}

zsvsheet_proc_id_t zsvsheet_register_proc(const char *name, const char *description, zsvsheet_proc_fn handler) {
  struct zsvsheet_procedure procedure = {
    .id = zsvsheet_generate_proc_id(), .name = name, .description = description, .handler = handler};
  return zsvsheet_do_register_proc(&procedure);
}
