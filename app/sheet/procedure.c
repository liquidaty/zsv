#include "procedure.h"
#include "darray.h"

#if 0
#define proc_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define proc_debug(...) ((void)0)
#endif

int proc_id_generator = 100; // TODO: shuold be dependant on number of bulitins
struct darray procedures = {};

/* Each procedure can be given a name and in the future argument specification
 * so it can be typed from the procedureline. Simple procedure like call motion
 * don't can omit a name meaning they can only be executed via key binding.
 */
struct zsvsheet_procedure {
  zsvsheet_proc_id_t id;
  const char *name;
  zsvsheet_proc_handler_fn handler;
};

struct zsvsheet_procedure *zsvsheet_find_procedure(zsvsheet_proc_id_t proc_id)
{
  darray_for(struct zsvsheet_procedure, proc, &procedures) {
    if(proc->id == proc_id)
      return proc;
  }
  return NULL;
}

zsvsheet_handler_status zsvsheet_proc_invoke(zsvsheet_proc_id_t proc_id, struct zsvsheet_proc_context *ctx)
{
  proc_debug("invoke proc %d\n", proc_id);
  struct zsvsheet_procedure *proc = zsvsheet_find_procedure(proc_id);
  if(proc) {
    proc_debug("call proc %d handler %p\n", proc->id, proc->handler);
    return proc->handler(ctx);
  }
  return -1;
}

zsvsheet_handler_status zsvsheet_proc_invoke_from_keypress(zsvsheet_proc_id_t proc_id, int ch, void *subcommand_context)
{
  struct zsvsheet_proc_context context = {
    .proc_id = proc_id,
    .invocation.type = zsvsheet_proc_invocation_type_keypress,
    .invocation.interactive = true,
    .invocation.u.keypress.ch = ch,
    .subcommand_context = subcommand_context,
  };
  return zsvsheet_proc_invoke(proc_id, &context);
}

static zsvsheet_proc_id_t zsvsheet_do_register_proc(struct zsvsheet_procedure *proc)
{
  proc_debug("register proc %d %s\n", proc->id, proc->name ? proc->name : "(unnamed)");
  if(zsvsheet_find_procedure(proc->id))
    return -1;

  /* Lazy init */
  if(!darray_alive(&procedures))
    darray_init(&procedures, sizeof(struct zsvsheet_procedure));
  darray_push(&procedures, proc);

  proc_id_generator = proc->id;
  return proc->id;
}

zsvsheet_proc_id_t zsvsheet_register_builtin_proc(zsvsheet_proc_id_t id, const char *name, zsvsheet_proc_handler_fn handler)
{
  struct zsvsheet_procedure procedure = {
    .id = id, .name = name, .handler = handler
  };
  return zsvsheet_do_register_proc(&procedure);
}

zsvsheet_proc_id_t zsvsheet_register_proc(const char *name, zsvsheet_proc_handler_fn handler)
{
  struct zsvsheet_procedure procedure = {
    .id = proc_id_generator + 1, .name = name, .handler = handler
  };
  return zsvsheet_do_register_proc(&procedure);
}
