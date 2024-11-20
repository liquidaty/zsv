#include "sql_internal.h"

struct zsv_sqlite3_db *zsv_sqlite3_db_new(const char *csv_filename, char in_memory, const char *opts_used,

                                          size_t max_cols, int sqlite3_flags) {
  struct zsv_sqlite3_db *zdb = calloc(1, sizeof(*zdb));
  if (!zdb) {
    perror(NULL);
    return NULL;
  }
  const char *db_url = in_memory ? "file::memory:" : "";
  zdb->rc = sqlite3_open_v2(db_url, &zdb->db, sqlite3_flags, NULL);
  if (zdb->rc == SQLITE_OK && zdb->db) {
    zdb->rc = sqlite3_create_module(zdb->db, "csv", &CsvModule, 0);
    if (zdb->rc == SQLITE_OK)
      zsv_sqlite3_add_csv(zdb, csv_filename, opts_used, max_cols);
  }
  if (zdb->rc != SQLITE_OK && !zdb->err_msg)
    zdb->err_msg = strdup(sqlite3_errstr(zdb->rc));
  return zdb;
}

void zsv_sqlite3_db_delete(struct zsv_sqlite3_db *zdb) {
  if (zdb && zdb->db)
    sqlite3_close(zdb->db);
  free(zdb);
}

static int create_virtual_csv_table(const char *fname, sqlite3 *db, const char *opts_used, int max_columns,
                                    char **err_msgp, int table_ix) {
  // TO DO: set customizable maximum number of columns to prevent
  // runaway in case no line ends found
  char *sql = NULL;
  char table_name_suffix[64];

  if (table_ix == 0)
    *table_name_suffix = '\0';
  else if (table_ix < 0 || table_ix > 1000)
    return -1;
  else
    snprintf(table_name_suffix, sizeof(table_name_suffix), "%i", table_ix + 1);

  if (max_columns)
    sql = sqlite3_mprintf("CREATE VIRTUAL TABLE data%s USING csv(filename=%Q,options_used=%Q,max_columns=%i)",
                          table_name_suffix, fname, opts_used, max_columns);
  else
    sql = sqlite3_mprintf("CREATE VIRTUAL TABLE data%s USING csv(filename=%Q,options_used=%Q)", table_name_suffix,
                          fname, opts_used);

  char *err_msg_tmp;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg_tmp);
  if (err_msg_tmp) {
    *err_msgp = strdup(err_msg_tmp);
    sqlite3_free(err_msg_tmp);
  }
  sqlite3_free(sql);
  return rc;
}

int zsv_sqlite3_add_csv(struct zsv_sqlite3_db *zdb, const char *csv_filename, const char *opts_used, size_t max_cols) {
  zdb->rc = create_virtual_csv_table(csv_filename, zdb->db, opts_used, max_cols, &zdb->err_msg, zdb->table_count);
  if (zdb->rc == SQLITE_OK)
    zdb->table_count++;
  return zdb->rc;
}
