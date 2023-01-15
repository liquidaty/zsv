static struct zsv_compare_added_column **
zsv_compare_added_column_add(struct zsv_compare_added_column **next,
                              struct zsv_compare_unique_colname *added_colname,
                              enum zsv_compare_status *stat
                              ) {
  struct zsv_compare_added_column *e = calloc(1, sizeof(*e));
  if(!e)
    *stat = zsv_compare_status_memory;
  else {
    e->colname = added_colname;
    *next = e;
    return &e->next;
  }
  return next;
}

static void zsv_compare_added_column_delete(struct zsv_compare_added_column *e) {
  for(struct zsv_compare_added_column *next; e; e = next) {
    next = e->next;
    free(e);
  }
}
