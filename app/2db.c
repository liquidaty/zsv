/*
 * Copyright (C) 2021-2022 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <sys/stat.h>
#include <errno.h>
#include <unistd.h> // unlink
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sqlite3.h>

#define ZSV_COMMAND 2db
#include "zsv_command.h"

#include <zsv/utils/mem.h>
#include <zsv/utils/string.h>
#include <zsv/utils/os.h>

#include <yajl_helper/yajl_helper.h>

#define ZSV_2DB_DEFAULT_TABLE_NAME "mytable"

enum zsv_2db_action {
  zsv_2db_action_create = 1,
  zsv_2db_action_append,
  zsv_2db_action_index
};

enum zsv_2db_state {
  zsv_2db_state_header = 1,
  zsv_2db_state_data,
  zsv_2db_state_done
};

#define LQ_2DB_MAX_INDEXES 32

struct zsv_2db_ix {
  struct zsv_2db_ix *next;
  char *name;
  char *on;
  char delete;
  char unique;
};

struct zsv_2db_column {
  struct zsv_2db_column *next;
  char *name;
  char *datatype;
  char *collate;
};

struct zsv_2db_options {
  char *table_name;
  char *db_fn;
  char verbose;
  char overwrite; // overwrite old db if it exists
#define ZSV_2DB_DEFAULT_BATCH_SIZE 10000
  size_t batch_size;
};

typedef struct zsv_2db_data *zsv_2db_handle;

struct zsv_2db_data {
  struct zsv_2db_options opts;

  char *db_fn_tmp;
  sqlite3 *db;
  char transaction_started;
  char *connection_string;

  struct {
    struct yajl_helper_parse_state st;
    yajl_status yajl_stat;
    enum zsv_2db_state state;

    unsigned int col_count;

    struct zsv_2db_column *columns, **last_column;
    struct zsv_2db_column current_column;
    struct zsv_2db_ix *indexes, **last_index;
    sqlite3_int64 index_sequence_num_max;
    struct zsv_2db_ix current_index;
    char have_row_data;

    char **row_values;

    sqlite3_stmt *insert_stmt;
    unsigned stmt_colcount;

  } json_parser;

  size_t rows_processed;
  size_t row_insert_attempts;
  size_t rows_inserted;
#define ZSV_2DB_MSG_BATCH_SIZE 10000 // number of rows between each console update (if verbose)

  int err;
};

static void zsv_2db_ix_free(struct zsv_2db_ix *e) {
  free(e->name);
  free(e->on);
}

static void zsv_2db_ixes_delete(struct zsv_2db_ix **p) {
  if(p && *p) {
    struct zsv_2db_ix *next;
    for(struct zsv_2db_ix *e = *p; e; e = next) {
      next = e->next;
      zsv_2db_ix_free(e);
      free(e);
    }
    *p = NULL;
  }
}

static void zsv_2db_column_free(struct zsv_2db_column *e) {
  free(e->name);
  free(e->datatype);
  free(e->collate);
}

static void zsv_2db_columns_delete(struct zsv_2db_column **p) {
  struct zsv_2db_column *next;
  if(p && *p) {
    for(struct zsv_2db_column *e = *p; e; e = next) {
      next = e->next;
      zsv_2db_column_free(e);
      free(e);
    }
    *p = NULL;
  }
}

static void zsv_2db_delete(zsv_2db_handle data) {
  if(!data) return;

  free(data->opts.table_name);
  free(data->db_fn_tmp);
  if(data->db)
    sqlite3_close(data->db);

  zsv_2db_columns_delete(&data->json_parser.columns);
  zsv_2db_column_free(&data->json_parser.current_column);

  zsv_2db_ixes_delete(&data->json_parser.indexes);
  zsv_2db_ix_free(&data->json_parser.current_index);

  free(data->json_parser.row_values);

  yajl_helper_parse_state_free(&data->json_parser.st);

  free(data);
}

/* sqlite3 helper functions */

static int zsv_2db_sqlite3_exec_2db(sqlite3 *db, const char *sql) {
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
  if(err_msg) {
    fprintf(stderr, "Error executing '%s': %s\n", sql, err_msg);
    sqlite3_free(err_msg);
  } else if(rc == SQLITE_DONE || rc == SQLITE_OK)
    return 0;
  return 1;
}
// add_db_indexes: return 0 on success, else error code
static int zsv_2db_add_indexes(struct zsv_2db_data *data) {
  int err = 0;
  for(struct zsv_2db_ix *ix = data->json_parser.indexes; !err && ix; ix = ix->next) {
    sqlite3_str *pStr = sqlite3_str_new(data->db);
    sqlite3_str_appendf(pStr, "create%s index \"%w_%w\" on \"%w\"(%s)",
                        ix->unique ? " unique" : "",
                        data->opts.table_name,
                        ix->name,
                        data->opts.table_name,
                        ix->on);
    err = zsv_2db_sqlite3_exec_2db(data->db, sqlite3_str_value(pStr));
    if(!err)
      data->json_parser.index_sequence_num_max++;
    sqlite3_free(sqlite3_str_finish(pStr));
  }
  return err;
}

static void zsv_2db_start_transaction(struct zsv_2db_data *data) {
  if(!data->transaction_started)
    sqlite3_exec(data->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
  data->transaction_started = 1;
}

static void zsv_2db_end_transaction(struct zsv_2db_data *data) {
  if(data->transaction_started)
    sqlite3_exec(data->db, "COMMIT", NULL, NULL, NULL);
  data->transaction_started = 0;
}

static sqlite3_str *build_create_table_statement(sqlite3 *db, const char *tname,
                                                 const char * const *colnames,
                                                 const char * const *datatypes,
                                                 const char * const *collates,
                                                 unsigned int col_count
                                                 ) {
  int err = 0;
  sqlite3_str *pStr = sqlite3_str_new(db);
  sqlite3_str_appendf(pStr, "CREATE TABLE \"%w\" (\n  ", tname ? tname : ZSV_2DB_DEFAULT_TABLE_NAME);
  for(unsigned int i = 0; i < col_count; i++) {
    if(i > 0)
      sqlite3_str_appendf(pStr, ",\n  ");
    sqlite3_str_appendf(pStr, "\"%w\"", colnames[i]);

    const char *datatype = datatypes ? datatypes[i] : NULL;
    if(!datatype || !(!strcmp("int", datatype) || !strcmp("integer", datatype) || !strcmp("real", datatype) || !strcmp("text", datatype))) {
      if(datatype)
        fprintf(stderr, "Unrecognized datatype %s", datatype), err = 1;
      else
        datatype = "text";
    }
    if(!err) {
      const char *collate = collates ? collates[i] : NULL;
      if(collate && *collate) {
        if(collate && !(!strcmp("binary", collate) || !strcmp("rtrim", collate) || !strcmp("nocase", collate))) {
          fprintf(stderr, "Unrecognized collate: expected binary, rtrim or nocase, got %s", collate);
          err = 1;
        } else
          sqlite3_str_appendf(pStr, " %s%s%s", datatype, collate ? " collate " : "", collate ? collate : "");
      }
    }
  }
  if(err) {
    if(pStr)
      sqlite3_free(sqlite3_str_finish(pStr));
    pStr = NULL;
  } else
    sqlite3_str_appendf(pStr, ")\n");
  return pStr;
}

// zsv_2db_finish_header: return 0 on error, 1 on success
static int zsv_2db_finish_header(struct zsv_2db_data *data) {
  if(data->err)
    return 0;
  if(!data->json_parser.col_count) {
    fprintf(stderr, "No columns found!\n");
    return 0;
  }

  data->json_parser.state = zsv_2db_state_data;
  if((data->json_parser.row_values = calloc(data->json_parser.col_count,
                                            sizeof(*data->json_parser.row_values))))
    return 1;

  data->err = 1;
  return 0;
}


/* json parser functions */

static sqlite3_stmt *create_insert_statement(sqlite3 *db, const char *tname,
                                             unsigned int col_count) {
  sqlite3_stmt *insert_stmt = NULL;
  sqlite3_str *insert_sql = sqlite3_str_new(db);
  if(insert_sql) {
    sqlite3_str_appendf(insert_sql, "insert into \"%w\" values(?", tname);
    for(unsigned int i = 1; i < col_count; i++)
      sqlite3_str_appendf(insert_sql, ", ?");
    sqlite3_str_appendf(insert_sql, ")");
    int status = sqlite3_prepare_v2(db, sqlite3_str_value(insert_sql),
                                    -1, &insert_stmt, NULL);
    if(status != SQLITE_OK) {
      fprintf(stderr, "Unable to prep (%s): %s\n", sqlite3_str_value(insert_sql),
              sqlite3_errmsg(db));
    }
    sqlite3_free(sqlite3_str_finish(insert_sql));
  }
  return insert_stmt;
}

// return error
static int zsv_2db_set_insert_stmt(struct zsv_2db_data *data) {
  int err = 0;
  if(!data->json_parser.col_count) {
    fprintf(stderr, "insert statement called with no columns to insert");
    err = 1;
  } else {
    const char **colnames = calloc(data->json_parser.col_count, sizeof(*colnames));
    const char **datatypes = calloc(data->json_parser.col_count, sizeof(*datatypes));
    const char **collates = calloc(data->json_parser.col_count, sizeof(*collates));
    unsigned int i = 0;
    for(struct zsv_2db_column *e = data->json_parser.columns; e; e = e->next, i++) {
      colnames[i] = e->name;
      datatypes[i] = e->datatype;
      collates[i] = e->collate;
    }

    sqlite3_str *create_sql =
      build_create_table_statement(data->db, data->opts.table_name,
                                   colnames, datatypes, collates,
                                   data->json_parser.col_count
                                   );
    if(!create_sql)
      err = 1;
    else {
      if(!(err = zsv_2db_sqlite3_exec_2db(data->db, sqlite3_str_value(create_sql)))
            && !(data->json_parser.insert_stmt =
                 create_insert_statement(data->db, data->opts.table_name,
                                         data->json_parser.col_count))) {
        err = 1;
        zsv_2db_start_transaction(data);
      } else
        data->json_parser.stmt_colcount = data->json_parser.col_count;
      sqlite3_free(sqlite3_str_finish(create_sql));
    }

    free(colnames);
    free(datatypes);
    free(collates);
  }
  return err;
}


/*
  add_local_db_row(): return sqlite3 error, or 0 on ok
*/
static int zsv_2db_insert_row_values(sqlite3_stmt *stmt, unsigned stmt_colcount,
                                     char const *const *const values,
                                     unsigned int values_count
                                     ) {
  if(!stmt)
    return -1;

  int status = 0;
  unsigned int errors_printed = 0;
  if(values_count > stmt_colcount)
    values_count = stmt_colcount;

  for(unsigned int i = 0; i < values_count; i++) {
    const char *val = values[i];
    if(val && *val)
      sqlite3_bind_text(stmt, (int)i+1, val, (int)strlen(val), SQLITE_STATIC);
    else
      // don't use sqlite3_bind_null, else x = ? will fail if value is ""/null
      sqlite3_bind_text(stmt, (int)i+1, "", 0, SQLITE_STATIC);
  }

  for(unsigned int i = values_count; i < stmt_colcount; i++)
    sqlite3_bind_null(stmt, (int)i+1);

  status = sqlite3_step(stmt);
  if(status == SQLITE_DONE)
    status = 0;
  else if(errors_printed < 10) {
    errors_printed++;
    fprintf(stderr, "Unable to insert: %s\n", sqlite3_errstr(status));
  } else if(errors_printed != 100) {
    errors_printed = 100;
    fprintf(stderr, "Too many insert errors to print\n");
  }

  sqlite3_reset(stmt);

  return status;
}

static int zsv_2db_insert_row(struct zsv_2db_data *data) {
  if(!data->err) {
    data->rows_processed++;
    if(data->json_parser.have_row_data) {
      if(!data->json_parser.insert_stmt)
        data->err = zsv_2db_set_insert_stmt(data);

      if(!data->db)
        return 0;
      int rc =
        zsv_2db_insert_row_values(data->json_parser.insert_stmt,
                                  data->json_parser.stmt_colcount,
                                  (char const *const *const) data->json_parser.row_values,
                                  data->json_parser.col_count);
      data->row_insert_attempts++;
      if(!rc) {
        data->rows_inserted++;
        if(data->opts.verbose && (data->rows_inserted % ZSV_2DB_MSG_BATCH_SIZE == 0))
          fprintf(stderr, "%zu rows inserted\n", data->rows_inserted);
        if(data->opts.batch_size && (data->rows_inserted % data->opts.batch_size == 0)) {
          zsv_2db_end_transaction(data);
          if(data->opts.verbose)
            fprintf(stderr, "%zu rows committed\n", data->rows_inserted);
          zsv_2db_start_transaction(data);
        }
      }
    }
  }

  return 1;
}

static int json_start_map(struct yajl_helper_parse_state *st) {
  (void)(st);
  return 1;
}

static int json_end_map(struct yajl_helper_parse_state *st) {
  struct zsv_2db_data *data = yajl_helper_data(st);
  if(data->json_parser.state == zsv_2db_state_header && yajl_helper_got_path(st, 3, "[{columns[")) { // exiting a column header
    if(!data->json_parser.current_column.name) {
      fprintf(stderr, "Name missing from column spec!\n");
      return 0;
    } else {
      struct zsv_2db_column *e = calloc(1, sizeof(*e));
      if(!e) {
        fprintf(stderr, "Out of memory!");
        return 0;
      }
      *e = data->json_parser.current_column;
      *data->json_parser.last_column = e;
      data->json_parser.last_column = &e->next;
      data->json_parser.col_count++;
      memset(&data->json_parser.current_column, 0, sizeof(data->json_parser.current_column));
    }
  } else if(data->json_parser.state == zsv_2db_state_header &&
            yajl_helper_got_path(st, 3, "[{indexes{")) { // exiting an index
    if(!data->json_parser.current_index.name) {
      fprintf(stderr, "Name missing from index spec\n");
      return 0;
    } else if(!(data->json_parser.current_index.on || data->json_parser.current_index.delete)) {
      fprintf(stderr, "'on' or 'delete' missing from index spec\n");
      return 0;
    } else {
      struct zsv_2db_ix *e = calloc(1, sizeof(*e));
      if(!e) {
        fprintf(stderr, "Out of memory!");
        return 0;
      }
      *e = data->json_parser.current_index;
      *data->json_parser.last_index = e;
      data->json_parser.last_index = &e->next;
      memset(&data->json_parser.current_index, 0, sizeof(data->json_parser.current_index));
    }
  }
  return 1;
}

static int json_map_key(struct yajl_helper_parse_state *st,
                        const unsigned char *s, size_t len) {
  struct zsv_2db_data *data = yajl_helper_data(st);
  if(data->json_parser.state == zsv_2db_state_header &&
     yajl_helper_got_path(st, 3, "[{indexes{")) {
    free(data->json_parser.current_index.name);
    if(len)
      data->json_parser.current_index.name = zsv_memdup(s, len);
    else
      data->json_parser.current_index.name = NULL;
  }
  return 1;
}

static int json_start_array(struct yajl_helper_parse_state *st) {
  if(yajl_helper_level(st) == 2) {
    struct zsv_2db_data *data = yajl_helper_data(st);
    if(data->json_parser.state == zsv_2db_state_header &&
       yajl_helper_got_path(st, 2, "[[")
       && yajl_helper_array_index_plus_1(st, 1) == 2)
      return zsv_2db_finish_header(data);
  }
  return 1;
}

static void reset_row_values(struct zsv_2db_data *data) {
  if(data->json_parser.row_values) {
    for(unsigned int i = 0; i < data->json_parser.col_count; i++) {
      free(data->json_parser.row_values[i]);
      data->json_parser.row_values[i] = NULL;
    }
  }
  data->json_parser.have_row_data = 0;
}

static int json_end_array(struct yajl_helper_parse_state *st) {
  if(yajl_helper_level(st) == 2) {
    struct zsv_2db_data *data = yajl_helper_data(st);
    if(data->json_parser.state == zsv_2db_state_data &&
       yajl_helper_got_path(st, 2, "[[")) { // finished a row of data
      zsv_2db_insert_row(data);
      reset_row_values(data);
    }
  }
  return 1;
}

static int json_process_value(struct yajl_helper_parse_state *st,
                              struct json_value *value) {
  const unsigned char *jsstr;
  size_t len;
  struct zsv_2db_data *data = yajl_helper_data(st);
  if(data->json_parser.state == zsv_2db_state_data) {
    if(yajl_helper_got_path(st, 3, "[[[")) {
      json_value_default_string(value, &jsstr, &len);
      if(jsstr && len) {
        unsigned int j = yajl_helper_array_index_plus_1(st, 0);
        if(j && j-1 < data->json_parser.col_count) {
          data->json_parser.row_values[j-1] = zsv_memdup(jsstr, len);
          data->json_parser.have_row_data = 1;
        }
      }
    }
  } else if(yajl_helper_got_path(st, 2, "[{name")) {
    json_value_default_string(value, &jsstr, &len);
    if(len) {
      if(data->opts.table_name)
        fprintf(stderr, "Table name specified twice; keeping %s, ignoring %.*s\n",
                data->opts.table_name, (int)len, jsstr);
      else
        data->opts.table_name = zsv_memdup(jsstr, len);
    }
  } else if(yajl_helper_got_path(st, 4, "[{columns[{name")) {
    free(data->json_parser.current_column.name);
    data->json_parser.current_column.name = NULL;
    json_value_default_string(value, &jsstr, &len);
    if(jsstr && len)
      data->json_parser.current_column.name = zsv_memdup(jsstr, len);
  } else if(yajl_helper_got_path(st, 4, "[{columns[{datatype")) {
    free(data->json_parser.current_column.datatype);
    data->json_parser.current_column.datatype = NULL;
    json_value_default_string(value, &jsstr, &len);
    if(jsstr && len)
      data->json_parser.current_column.datatype = zsv_memdup(jsstr, len);
  } else if(yajl_helper_got_path(st, 4, "[{columns[{collate")) {
    free(data->json_parser.current_column.collate);
    data->json_parser.current_column.collate = NULL;
    json_value_default_string(value, &jsstr, &len);
    if(jsstr && len)
      data->json_parser.current_column.collate = zsv_memdup(jsstr, len);
  } else if(yajl_helper_got_path(st, 4, "[{indexes{*{delete")) {
    data->json_parser.current_index.delete = json_value_truthy(value);
  } else if(yajl_helper_got_path(st, 4, "[{indexes{*{unique")) {
    data->json_parser.current_index.unique = json_value_truthy(value);
  } else if(yajl_helper_got_path(st, 4, "[{indexes{*{on")
            || yajl_helper_got_path(st, 5, "[{indexes{*{on[")) {
    json_value_default_string(value, &jsstr, &len);
    if(len) {
      if(yajl_helper_level(st) == 4 || !data->json_parser.current_index.on) {
        free(data->json_parser.current_index.on);
        data->json_parser.current_index.on = zsv_memdup(jsstr, len);
      } else {
        char *defn;
        asprintf(&defn, "%s,%.*s", data->json_parser.current_index.on, (int)len, jsstr);
        free(data->json_parser.current_index.on);
        data->json_parser.current_index.on = defn;
      }
    }
  }
  return 1;
}

/* api functions */

// exportable
static zsv_2db_handle zsv_2db_new(struct zsv_2db_options *opts) {
  int err = 0;
  if(!opts->db_fn)
    fprintf(stderr, "Please specify an output file\n"), err = 1;

  struct stat stt = { 0 };
  if(!err && !opts->overwrite && (!stat(opts->db_fn, &stt) || errno != ENOENT))
    fprintf(stderr, "File %s already exists\n", opts->db_fn), err = 1;

  if(err)
    return NULL;

  struct zsv_2db_data *data = calloc(1, sizeof(*data));
  data->opts = *opts;

  if(!(data->opts.batch_size))
    data->opts.batch_size = ZSV_2DB_DEFAULT_BATCH_SIZE;

  data->json_parser.last_column = &data->json_parser.columns;
  data->json_parser.last_index = &data->json_parser.indexes;
  data->json_parser.state = zsv_2db_state_header;
  if(opts->table_name)
    data->opts.table_name = strdup(opts->table_name);
  asprintf(&data->db_fn_tmp, "%s.tmp", data->opts.db_fn);
  if(!data->db_fn_tmp)
    err = 1;
  else {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
#ifndef NDEBUG
    fprintf(stderr, "Opening: %s\n", data->db_fn_tmp);
#endif
    unlink(data->db_fn_tmp);
    int rc = sqlite3_open_v2(data->db_fn_tmp, &data->db, flags, NULL);
    err = 1;
    if(!data->db)
      fprintf(stderr, "Unable to open db at %s\n", data->db_fn_tmp);
    else if(rc != SQLITE_OK )
      fprintf(stderr, "Unable to open db at %s: %s\n", data->db_fn_tmp,
              sqlite3_errmsg(data->db));
    else {
      err = 0;

      // performance tweaks
      sqlite3_exec(data->db, "PRAGMA synchronous = OFF", NULL, NULL, NULL);
      sqlite3_exec(data->db, "PRAGMA journal_mode = OFF", NULL, NULL, NULL);

      // parse the input and create & populate the database table
      if(yajl_helper_parse_state_init(&data->json_parser.st, 32,
                                      json_start_map, json_end_map, json_map_key,
                                      json_start_array, json_end_array,
                                      json_process_value,
                                      data) != yajl_status_ok) {
        fprintf(stderr, "Unable to get yajl parser\n");
        err = 1;
      } else {
//        yajl_helper_callbacks_init(&data->json_parser.callbacks, 32);
//        data->json_parser.handle = st->yajl; // yajl_alloc(&data->json_parser.callbacks, NULL,
        // &data->json_parser.st);
      }
    }
  }

  if(err) {
    zsv_2db_delete(data);
    data = NULL;
  }

  return data;
}

// exportable
static int zsv_2db_err(zsv_2db_handle h) {
  return h->err;
}

// exportable
static int zsv_2db_finish(zsv_2db_handle data) {
  // add indexes
  int err = zsv_2db_add_indexes(data);
  if(!err) {
    if(data->db) {
      zsv_2db_end_transaction(data);
      if(data->json_parser.insert_stmt)
        sqlite3_finalize(data->json_parser.insert_stmt);

      sqlite3_close(data->db);
      data->db = NULL;

       // rename tmp to target
      unlink(data->opts.db_fn);
      if(zsv_replace_file(data->db_fn_tmp, data->opts.db_fn)) {
        fprintf(stderr, "Unable to rename %s to %s\n",
                data->db_fn_tmp, data->opts.db_fn);
        zsv_perror(NULL);
        err = 1;
      } else
        fprintf(stderr, "Database %s created\n", data->opts.db_fn);
    }
  }
  return err;
}

// exportable
static yajl_handle zsv_2db_yajl_handle(zsv_2db_handle data) {
  return data->json_parser.st.yajl;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *zsv_opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  (void)(zsv_opts);
  (void)(opts_used);
  FILE *f_in = NULL;
  int err = 0;
  struct zsv_2db_options opts = { 0 };
  opts.verbose = zsv_get_default_opts().verbose;

  const char *usage[] =
    {
     APPNAME ": convert JSON to sqlite3",
     "",
     "Usage: " APPNAME " -o <output path> [-t <table name>] [input.json]\n",
     "",
     "Alternatively, --output may be used in lieu of -o",
     "",
     "Options:",
     "  -h,--help",
     "  --table <table_name> : save as specified table name",
     "  --overwrite          : overwrite existing database",
     // to do:
     // --sql to output sql statements
     // --append: append to existing db
     "",
     "Converts a json representation of a database table to an sqlite3 file.",
     "",

     // TO DO: add output examples and schema descriptions
     "The input should conform to the schema defined at:",
     "  https://github.com/liquidaty/zsv/blob/main/app/schema/database-table.json",
     "",
     "For example:",
     "  [",
     "    {",
     "      \"columns\":[{\"name\":\"column 1\"}],",
     "      \"indexes\":{\"ix1\":{\"on\":\"[column 1]\",\"unique\":true}}",
     "    },",
     "    [",
     "      [\"row 1 cell 1\"],",
     "      [\"row 2 cell 1\"]",
     "    ]",
     "  ]",
     NULL
    };

  for(int i = 1; !err && i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      for(int j = 0; usage[j]; j++)
        fprintf(stdout, "%s\n", usage[j]);
      goto exit_2db;
    } else if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = 1;
      else if(opts.db_fn)
        fprintf(stderr, "Output file specified more than once (%s and %s)\n",
                opts.db_fn, argv[i]), err = 1;
      else
        opts.db_fn = (char *)argv[i]; // we won't free this
    } else if(!strcmp(argv[i], "--overwrite")) {
      opts.overwrite = 1;
    } else if(!strcmp(argv[i], "--table")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = 1;
      else if(opts.table_name)
        fprintf(stderr, "Table name specified more than once (%s and %s)\n",
                opts.table_name, argv[i]), err = 1;
      else
        opts.table_name = (char *)argv[i]; // we won't free this
    } else if(f_in)
      fprintf(stderr, "Input file specified more than once\n"), err = 1;
    else if(!(f_in = fopen(argv[i], "rb")))
      fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = 1;
    else if(!(strlen(argv[i]) > 5 && !zsv_stricmp((const unsigned char *)argv[i] + strlen(argv[i]) - 5, (const unsigned char *)".json")))
      fprintf(stderr, "Warning: input filename does not end with .json (%s)\n", argv[i]);
  }

  if(!f_in) {
#ifdef NO_STDIN
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
#else
    f_in = stdin;
#endif
  }

  if(!err) {
    zsv_2db_handle data = zsv_2db_new(&opts);
    if(!data)
      err = 1;
    else {
      size_t chunk_size = 4096*16;
      unsigned char *buff = malloc(chunk_size);
      if(!buff)
        err = 1;
      else {
        size_t bytes_read = 0;
        while(!err && !zsv_2db_err(data)) {
          bytes_read = fread(buff, 1, chunk_size, f_in);
          if(bytes_read == 0)
            break;
          yajl_status stat = yajl_parse(zsv_2db_yajl_handle(data), buff, bytes_read);
          if(stat != yajl_status_ok)
            err = yajl_helper_print_err(data->json_parser.st.yajl, buff, bytes_read);
        }

        if(!err) {
          if(yajl_complete_parse(zsv_2db_yajl_handle(data)) != yajl_status_ok)
            err = yajl_helper_print_err(data->json_parser.st.yajl, buff, bytes_read);
          else if(zsv_2db_err(data) || zsv_2db_finish(data))
            err = 1;
        }
        free(buff);
      }
      zsv_2db_delete(data);
    }
  }

 exit_2db:
  if(f_in && f_in != stdin)
    fclose(f_in);

  return err;
}
