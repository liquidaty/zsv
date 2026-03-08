static void zsv_select_search_str_delete(struct zsv_select_search_str *ss) {
  for (struct zsv_select_search_str *next; ss; ss = next) {
    next = ss->next;
    free(ss);
  }
}

static void zsv_select_add_search(struct zsv_select_data *data, const char *value) {
  struct zsv_select_search_str *ss = calloc(1, sizeof(*ss));
  ss->value = value;
  ss->len = value ? strlen(value) : 0;
  ss->next = data->search_strings;
  data->search_strings = ss;
}
