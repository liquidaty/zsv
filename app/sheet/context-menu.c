#include "procedure.h"

#ifndef MIN
# define MIN(A,B) ((A)<(B)?(A):(B))
#endif
#ifndef MAX
# define MAX(A,B) ((A)>(B)?(A):(B))
#endif

struct context_menu_entry {
  const char *name;
  zsvsheet_proc_id_t proc_id;
  struct context_menu_entry *next;
  /* Entries can be created on stack and linked to the menu but when using
   * using context_menu_add_entry* the entries are allocated.
   * This bit indicates whether or not this entry should be freed on cleanup.
   */
  uint8_t allocated : 1;
};


struct context_menu {
  struct context_menu_entry *entries;
  struct context_menu_entry *last_entry;
};

/* Context menu linking to existing procedure */
int context_menu_entry_init(
    struct context_menu_entry *entry, const char *name) 
{
  memset(entry, 0, sizeof(*entry));
  entry->name = name;
  return 0;
}

int context_menu_init(struct context_menu *menu)
{
  memset(menu, 0, sizeof(*menu));
  return 0;
}

void context_menu_add_entry(
    struct context_menu *menu, struct context_menu_entry *entry)
{
  if(menu->last_entry) {
    menu->last_entry->next = entry;
    menu->last_entry = entry;
  } else {
    assert(menu->entries == NULL);
    menu->entries = menu->last_entry = entry;
  }
}

int context_menu_new_entry(
    struct context_menu *menu, const char *name)
{
  struct context_menu_entry *entry = malloc(sizeof(struct context_menu_entry));
  if(!entry) 
    return -ENOMEM;
  if(context_menu_entry_init(entry, name))
    return -1;
  entry->allocated = true;
  context_menu_add_entry(menu, entry);
  return 0;
}

int context_menu_display(
    struct context_menu *menu, int row, int col, struct zsvsheet_display_dimensions *display_dims)
{
  int padding = 2;
  int menu_width = 0, menu_height = 0;

  for(struct context_menu_entry *entry = menu->entries; entry; entry = entry->next) {
    int len = strlen(entry->name);
    menu_width = MAX(menu_width, len);
    menu_height += 1;
  }
  /* add padding */
  menu_width += padding * 2;

  /* Make sure we don't overrun the screen horizontally */
  col -= MAX(0, col + menu_width - (int)display_dims->columns);

  /* TODO: don't write over footer */
  if(row + menu_height > display_dims->rows)
    row -= menu_height;
  else
    row -= 1; /* Display the menu under the selected cell by default */

  attron(A_REVERSE);
  int i = 0;
  for(struct context_menu_entry *entry = menu->entries; entry; entry = entry->next)
  {
    mvprintw(row + i, col, "%-*s", menu_width, "");
    mvprintw(row + i, col + padding, "%s", entry->name);
    i += 1;
  }
  attroff(A_REVERSE);
  return 0;
}

int context_menu_cleanup(struct context_menu *menu)
{
  struct context_menu_entry *entry = menu->entries, *next;
  while(entry) {
    next = entry->next;
    entry->next = NULL;
    if(entry->allocated)
      free(entry);
    entry = next;
  }
  memset(menu, 0, sizeof(*menu));
}

