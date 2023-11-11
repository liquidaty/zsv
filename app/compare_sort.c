/**
 * To implement sorting, we will use sqlite, create a table for each CSV file and run "select * order by ..."
 */

static int zsv_compare_sort_prep_table(struct zsv_compare_data *data,
                                       const char *fname,
                                       const char *opts_used,
                                       int max_columns,
                                       char **err_msg,
                                       unsigned int table_ix
                                       ) {
#define ZSV_COMPARE_MAX_TABLES 1000
  char *sql = NULL;
  if(table_ix > ZSV_COMPARE_MAX_TABLES)
    return -1;

  if(max_columns == 0)
    max_columns = 2048;

  sql = sqlite3_mprintf("CREATE VIRTUAL TABLE data%i USING csv(filename=%Q,options_used=%Q,max_columns=%i)", table_ix, fname, opts_used, max_columns);
  if(!sql)
    return -1;

  int rc = sqlite3_exec(data->sort_db, sql, NULL, NULL, err_msg);
  sqlite3_free(sql);
  return rc;
}

static int zsv_compare_sort_stmt_prep(sqlite3 *db, sqlite3_stmt **stmtp,
                                      // struct zsv_compare_sort *sort,
                                      struct zsv_compare_key *keys,
                                      unsigned ix) {
  sqlite3_str *select_clause = sqlite3_str_new(db);
  if(!select_clause) {
    fprintf(stderr, "Out of memory!\n");
    return -1;
  }

  sqlite3_str_appendf(select_clause, "select * from data%i order by ", ix);
  for(struct zsv_compare_key *key = keys; key; key = key->next)
    sqlite3_str_appendf(select_clause, "%s\"%w\"", key == keys ? "" : ", ", key->name);

  int rc = sqlite3_prepare_v2(db, sqlite3_str_value(select_clause), -1, stmtp, NULL);
  if(rc != SQLITE_OK)
    fprintf(stderr, "%s: %s\n", sqlite3_errstr(rc), sqlite3_str_value(select_clause));
  sqlite3_free(sqlite3_str_finish(select_clause));
  return rc;
}

static enum zsv_compare_status
input_init_sorted(struct zsv_compare_data *data,
                  struct zsv_compare_input *input,
                  struct zsv_opts *_opts,
                  struct zsv_prop_handler *_prop_handler,
                  const char *opts_used
                  ) {
  (void)(_opts);
  (void)(_prop_handler);
  char *err_msg = NULL;
  int rc = zsv_compare_sort_prep_table(data, input->path, opts_used, 0, &err_msg, input->index);
  if(err_msg) {
    fprintf(stderr, "%s\n", err_msg);
    sqlite3_free(err_msg);
  }
  if(rc == SQLITE_OK)
    rc = zsv_compare_sort_stmt_prep(data->sort_db, &input->sort_stmt,
                                    data->keys,
                                    input->index);
  return rc == SQLITE_OK ? zsv_compare_status_ok : zsv_compare_status_error;
}

static enum zsv_status zsv_compare_next_sorted_row(struct zsv_compare_input *input) {
  if(sqlite3_step(input->sort_stmt) == SQLITE_ROW)
    return zsv_status_row;

  // to do: check if error; if so return zsv_status_error
  return zsv_status_done;
}

static struct zsv_cell zsv_compare_get_sorted_colname(struct zsv_compare_input *input, unsigned ix) {
  struct zsv_cell c;
  c.str = (unsigned char *)sqlite3_column_name(input->sort_stmt, (int)ix);
  c.len = c.str ? strlen((const char *)c.str) : 0;
  c.quoted = 1;
  return c;
}

static unsigned zsv_compare_get_sorted_colcount(struct zsv_compare_input *input) {
  int col_count = sqlite3_column_count(input->sort_stmt);
  if(col_count >= 0)
    return (unsigned) col_count;
  return 0;
}


static struct zsv_cell zsv_compare_get_sorted_cell(struct zsv_compare_input *input, unsigned ix) {
  struct zsv_cell c;
  c.str = (unsigned char *)sqlite3_column_text(input->sort_stmt, (int)ix);
  c.len = c.str ? sqlite3_column_bytes(input->sort_stmt, (int)ix) : 0;
  if(c.len)
    c.str = (unsigned char *)zsv_strtrim(c.str, &c.len);
  c.quoted = 1;
  return c;
}
