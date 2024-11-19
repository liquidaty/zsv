#ifndef SQL_INTERNAL_H
#define SQL_INTERNAL_H

extern sqlite3_module CsvModule;

struct zsv_sqlite3_db {
  sqlite3 *db;
  int table_count;
  char *err_msg;
  int rc;
};

struct zsv_sqlite3_db *zsv_sqlite3_db_new(const char *csv_filename, char in_memory, const char *opts_used,
                                          size_t max_cols, int sqlite3_flags);

void zsv_sqlite3_db_delete(struct zsv_sqlite3_db *zdb);

int zsv_sqlite3_add_csv(struct zsv_sqlite3_db *zdb, const char *csv_filename, const char *opts_used, size_t max_cols);

#endif
