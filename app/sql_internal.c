#include "sql_internal.h"

struct zsv_sqlite3_db *zsv_sqlite3_db_new(struct zsv_sqlite3_dbopts *dbopts) {
  struct zsv_sqlite3_db *zdb = calloc(1, sizeof(*zdb));
  if (!zdb) {
    perror(NULL);
    return NULL;
  }
  const char *db_url = dbopts && dbopts->in_memory ? "file::memory:" : "";
  int flags = SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE;
  zdb->rc = sqlite3_open_v2(db_url, &zdb->db, flags, NULL);
  if (zdb->rc == SQLITE_OK && zdb->db)
    zdb->rc = sqlite3_create_module(zdb->db, "csv", &CsvModule, 0);
  if (zdb->rc != SQLITE_OK && !zdb->err_msg)
    zdb->err_msg = strdup(sqlite3_errstr(zdb->rc));
  return zdb;
}

void zsv_sqlite3_db_delete(struct zsv_sqlite3_db *zdb) {
  if (zdb) {
    for (struct zsv_sqlite3_csv_file *next, *zcf = zdb->csv_files; zcf; zcf = next) {
      next = zcf->next;
      if (zcf->added)
        sqlite3_zsv_list_remove(zcf->path);
      free(zcf->path);
      free(zcf);
    }
    if (zdb->db)
      sqlite3_close(zdb->db);
    free(zdb);
  }
}

static int create_virtual_csv_table(const char *fname, sqlite3 *db, // int max_columns,
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

  sql = sqlite3_mprintf("CREATE VIRTUAL TABLE data%s USING csv(filename=%Q)", table_name_suffix, fname);

  char *err_msg_tmp;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg_tmp);
  if (err_msg_tmp) {
    *err_msgp = strdup(err_msg_tmp);
    sqlite3_free(err_msg_tmp);
  }
  sqlite3_free(sql);
  return rc;
}

int zsv_sqlite3_add_csv(struct zsv_sqlite3_db *zdb, const char *csv_filename, struct zsv_opts *opts,
                        struct zsv_prop_handler *custom_prop_handler) {
  struct zsv_sqlite3_csv_file *zcf = calloc(1, sizeof(*zcf));
  if (!zcf || !(zcf->path = strdup(csv_filename)))
    zdb->rc = SQLITE_ERROR;
  else {
    int err = 0;
    if (opts) {
      err = sqlite3_zsv_list_add(csv_filename, opts, custom_prop_handler);
      if (!err)
        zcf->added = 1;
    }
    if (err)
      zdb->rc = SQLITE_ERROR;
    else {
      zcf->next = zdb->csv_files;
      zdb->csv_files = zcf;
      zdb->rc = create_virtual_csv_table(csv_filename, zdb->db, &zdb->err_msg, zdb->table_count);
      if (zdb->rc == SQLITE_OK) {
        zdb->table_count++;
        return SQLITE_OK;
      }
    }
  }
  if (zcf) {
    free(zcf->path);
    free(zcf);
  }
  return zdb->rc;
}

/**
 * zsv_sqlite3_add_csv_no_dq is the same as zsv_sqlite3_add_csv() but also executes
 * sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, (void*)0);
 */
int zsv_sqlite3_add_csv_no_dq(struct zsv_sqlite3_db *zdb, const char *csv_filename, struct zsv_opts *opts,
                              struct zsv_prop_handler *custom_prop_handler) {
  int rc = zsv_sqlite3_add_csv(zdb, csv_filename, opts, custom_prop_handler);
  if (rc == SQLITE_OK)
    sqlite3_db_config(zdb->db, SQLITE_DBCONFIG_DQS_DDL, 0, (void *)0);
  return rc;
}
