#ifndef SQL_INTERNAL_H
#define SQL_INTERNAL_H

extern sqlite3_module CsvModule;

struct zsv_sqlite3_csv_file {
  struct zsv_sqlite3_csv_file *next;
  char *path;
  unsigned char added : 1;
  unsigned char _ : 7;
};

struct zsv_sqlite3_db {
  sqlite3 *db;
  int table_count;
  char *err_msg;
  struct zsv_sqlite3_csv_file *csv_files;
  int rc;
};

struct zsv_sqlite3_dbopts {
  unsigned char in_memory : 1;
  unsigned char _ : 7;
  // int sqlite3_flags
};

struct zsv_sqlite3_db *zsv_sqlite3_db_new(struct zsv_sqlite3_dbopts *dbopts);
void zsv_sqlite3_db_delete(struct zsv_sqlite3_db *zdb);

/**
 * @param opts: if non-null, opts and custom_prop_handler will be saved for use by the sqlite3 csvModule
 *              and the saved entry will be removed by zsv_sqlite3_db_delete()
 *              if NULL, csvModule will rely on any previously saved opts/custom_prop_handler
 */
int zsv_sqlite3_add_csv(struct zsv_sqlite3_db *zdb, const char *csv_filename, struct zsv_opts *opts,
                        struct zsv_prop_handler *custom_prop_handler, const char *opts_used);
#endif
