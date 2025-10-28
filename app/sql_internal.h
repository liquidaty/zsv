#ifndef SQL_INTERNAL_H
#define SQL_INTERNAL_H

#include <stdlib.h>
#include <string.h>
#include "external/sqlite3/sqlite3.h"
#include "external/sqlite3/sqlite3_csv_vtab-mem.h"
#include <zsv/utils/prop.h>
#include <zsv/utils/string.h>

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

#include "../include/zsv/utils/sql.h"

struct zsv_sqlite3_db *zsv_sqlite3_db_new(struct zsv_sqlite3_dbopts *dbopts);
void zsv_sqlite3_db_delete(struct zsv_sqlite3_db *zdb);

/**
 * @param opts: if non-null, opts and custom_prop_handler will be saved for use by the sqlite3 csvModule
 *              and the saved entry will be removed by zsv_sqlite3_db_delete()
 *              if NULL, csvModule will rely on any previously saved opts/custom_prop_handler
 */
int zsv_sqlite3_add_csv(struct zsv_sqlite3_db *zdb, const char *csv_filename, struct zsv_opts *opts,
                        struct zsv_prop_handler *custom_prop_handler);

/**
 * zsv_sqlite3_add_csv_no_dq is the same as zsv_sqlite3_add_csv() but also disables double-quoted
 * values from being interpreted as literals by executing the below:
 *   sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, (void*)0);
 */
int zsv_sqlite3_add_csv_no_dq(struct zsv_sqlite3_db *zdb, const char *csv_filename, struct zsv_opts *opts,
                              struct zsv_prop_handler *custom_prop_handler);

#endif
