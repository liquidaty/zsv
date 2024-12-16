#include "procedure.h"

#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

struct context_menu_entry {
  struct context_menu_entry *prev;
  struct context_menu_entry *next;
  char *name;
  zsvsheet_proc_id_t proc_id;
  /* Entries can be created on stack and linked to the menu but when using
   * using context_menu_add_entry* the entries are allocated.
   * This bit indicates whether or not this entry should be freed on cleanup.
   */
  uint8_t allocated : 1;
  uint8_t proc_ephemeral : 1;
};

struct context_menu {
  struct context_menu_entry *entries;
  struct context_menu_entry *last_entry;
  struct context_menu_entry *selected_entry;
};

int context_menu_init(struct context_menu *menu) {
  memset(menu, 0, sizeof(*menu));
  return 0;
}

void context_menu_add_entry(struct context_menu *menu, struct context_menu_entry *entry) {
  if (menu->last_entry) {
    menu->last_entry->next = entry;
    entry->prev = menu->last_entry;
    menu->last_entry = entry;
    menu->last_entry->next = NULL;
  } else {
    assert(menu->entries == NULL);
    menu->entries = menu->last_entry = entry;
    menu->last_entry->next = menu->last_entry->prev = NULL;
    menu->selected_entry = entry;
  }
}

int _context_menu_alloc_entry(struct context_menu *menu, const char *name, zsvsheet_proc_id_t proc_id,
                              bool proc_ephemeral) {
  struct context_menu_entry *entry = malloc(sizeof(struct context_menu_entry));
  if (!entry)
    return -ENOMEM;
  memset(entry, 0, sizeof(*entry));
  entry->name = strdup(name);
  entry->proc_id = proc_id;
  entry->allocated = true;
  entry->proc_ephemeral = proc_ephemeral;
  context_menu_add_entry(menu, entry);
  return 0;
}

/* New entry calling existing procedure */
int context_menu_new_entry(struct context_menu *menu, const char *name, zsvsheet_proc_id_t proc_id) {
  return _context_menu_alloc_entry(menu, name, proc_id, false);
}

/* New entry calling existing a newly created procedure */
int context_menu_new_entry_func(struct context_menu *menu, const char *name, zsvsheet_proc_fn handler) {
  zsvsheet_proc_id_t proc_id = zsvsheet_register_proc(NULL, NULL, handler);
  if (!is_valid_proc_id(proc_id))
    return -EINVAL;
  return _context_menu_alloc_entry(menu, name, proc_id, true);
}

int context_menu_display(struct context_menu *menu, int row, int col,
                         struct zsvsheet_display_dimensions *display_dims) {
  int padding = 2;
  int menu_width = 0, menu_height = 0;

  for (struct context_menu_entry *entry = menu->entries; entry; entry = entry->next) {
    int len = strlen(entry->name);
    menu_width = MAX(menu_width, len);
    menu_height += 1;
  }
  /* add padding */
  menu_width += padding * 2;

  /* Make sure we don't overrun the screen horizontally */
  col -= MAX(0, col + menu_width - (int)display_dims->columns);

  if (row + menu_height > (int)(display_dims->rows - display_dims->footer_span - display_dims->header_span))
    row -= menu_height;
  else
    row += 1; /* Display the menu under the selected cell by default */

  attron(A_REVERSE);
  int i = 0;
  for (struct context_menu_entry *entry = menu->entries; entry; entry = entry->next) {
    mvprintw(row + i, col, "%-*s", menu_width, "");
    if (entry == menu->selected_entry)
      attron(A_UNDERLINE);
    mvprintw(row + i, col + padding, "%s", entry->name);
    if (entry == menu->selected_entry) {
      attroff(A_UNDERLINE);
      if (padding) /* add a lil star if we have space */
        mvprintw(row + i, col, "*");
    }
    i += 1;
  }
  attroff(A_REVERSE);
  return 0;
}

zsvsheet_status context_menu_confirm(struct context_menu *menu, void *subcommand_context) {
  if (!menu->selected_entry)
    return zsvsheet_status_error;

  zsvsheet_proc_id_t proc_id = menu->selected_entry->proc_id;
  struct zsvsheet_proc_context context = {
    .proc_id = proc_id,
    .invocation.type = zsvsheet_proc_invocation_type_context_menu,
    .invocation.interactive = true,
    .invocation.u.context_menu.entry = menu->selected_entry,
    .subcommand_context = subcommand_context,
  };
  return zsvsheet_proc_invoke(proc_id, &context);
}

void context_menu_move_up(struct context_menu *menu) {
  if (menu->selected_entry && menu->selected_entry->prev)
    menu->selected_entry = menu->selected_entry->prev;
}

void context_menu_move_down(struct context_menu *menu) {
  if (menu->selected_entry && menu->selected_entry->next)
    menu->selected_entry = menu->selected_entry->next;
}

void context_menu_cleanup(struct context_menu *menu) {
  struct context_menu_entry *entry = menu->entries, *next;
  while (entry) {
    next = entry->next;
    entry->next = NULL;
    free(entry->name);
    if (entry->proc_ephemeral)
      zsvsheet_unregister_proc(entry->proc_id);
    if (entry->allocated)
      free(entry);
    entry = next;
  }
  memset(menu, 0, sizeof(*menu));
}
