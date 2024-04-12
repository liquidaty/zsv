static int zsv_compare_unique_colname_cmp(zsv_compare_unique_colname *x, zsv_compare_unique_colname *y) {
  return x->instance_num == y->instance_num ?
    zsv_stricmp(x->name, y->name) : x->instance_num > y->instance_num ? 1 : x->instance_num < y->instance_num ? -1 : 0;
}

SGLIB_DEFINE_RBTREE_FUNCTIONS(zsv_compare_unique_colname, left, right, color, zsv_compare_unique_colname_cmp);

static void zsv_compare_unique_colname_delete(zsv_compare_unique_colname *e) {
  if(e)
    free(e->name);
  free(e);
}

static void zsv_compare_unique_colnames_delete(zsv_compare_unique_colname **tree) {
  if(tree && *tree) {
    struct sglib_zsv_compare_unique_colname_iterator it;
    struct zsv_compare_unique_colname *e;
    for(e=sglib_zsv_compare_unique_colname_it_init(&it,*tree); e; e=sglib_zsv_compare_unique_colname_it_next(&it))
      zsv_compare_unique_colname_delete(e);
    *tree = NULL;
  }
}

static struct zsv_compare_unique_colname *
zsv_compare_unique_colname_new(const unsigned char *name, size_t len,
                               unsigned instance_num) {
  zsv_compare_unique_colname *col = calloc(1, sizeof(*col));
  if(!col || !(col->name = malloc(len + 1)))
    ; // handle out-of-memory error!
  else {
    memcpy(col->name, name, len);
    col->name[len] = '\0';
    col->name_len = len;
    col->instance_num = instance_num;
  }
  return col;
}

// zsv_desc_column_update_unique(): return 1 if unique, 0 if dupe
static zsv_compare_unique_colname *
zsv_compare_unique_colname_add_if_not_found(struct zsv_compare_unique_colname **tree,
                                            const unsigned char *utf8_value, size_t len,
                                            unsigned instance_num,
                                            int *added) {
  *added = 0;
  zsv_compare_unique_colname *col = zsv_compare_unique_colname_new(utf8_value, len, instance_num);
  zsv_compare_unique_colname *found = sglib_zsv_compare_unique_colname_find_member(*tree, col);
  if(found) // not unique
    zsv_compare_unique_colname_delete(col);
  else {
    sglib_zsv_compare_unique_colname_add(tree, col);
    *added = 1;
    found = col;
  }
  return found;
}

// add a colname to a list. allow duplicate names, but track instances
// separately (i.e.
static enum zsv_compare_status
zsv_compare_unique_colname_add(zsv_compare_unique_colname **tree,
                               const unsigned char *s,
                               unsigned len,
                               zsv_compare_unique_colname **new_col) {
  int added = 0;
  unsigned instance_num = 0;
  zsv_compare_unique_colname *_new_col =
    zsv_compare_unique_colname_add_if_not_found(tree, s, len,
                                                instance_num, &added);
  if(!_new_col)
    return zsv_compare_status_error;

  if(!added) { // we've seen this column before in this input
    instance_num = ++_new_col->total_instances;
    _new_col =
      zsv_compare_unique_colname_add_if_not_found(tree, s, len,
                                                  instance_num, &added);
    if(!added) { // should not happen
#ifndef NDEBUG
      fprintf(stderr, "Unexpected error! %s: %i\n", __FILE__, __LINE__);
#endif
      return zsv_compare_status_error;
    }
  }
  *new_col = _new_col;
  return zsv_compare_status_ok;
}
