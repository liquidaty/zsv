#ifndef _ZSVSHEET_CONTEXT_MENU_H_
#define _ZSVSHEET_CONTEXT_MENU_H_
#include "procedure.h"

struct context_menu_entry;

struct context_menu {
  struct context_menu_entry *entries;
  struct context_menu_entry *last_entry;
  struct context_menu_entry *selected_entry;
};

int context_menu_init(struct context_menu *menu);

int context_menu_new_entry(struct context_menu *menu, const char *name, zsvsheet_proc_id_t proc_id);

int context_menu_new_entry_func(struct context_menu *menu, const char *name, zsvsheet_proc_fn handler);

zsvsheet_status context_menu_confirm(struct context_menu *menu, void *subcommand_context);

#endif /* _ZSVSHEET_CONTEXT_MENU_H_ */
