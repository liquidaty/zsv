/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "../external/sqlite3/sqlite3.h"
#include <zsv/ext/implementation.h>
#include <zsv/ext/sheet.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/prop.h>
// #include "../curses.h"

/**
 * This is an example to demonstrate various extension capabilities
 * specific to the `sheet` subcommand. For examples of extending the
 * CLI outside of `sheet`, see my_extension.c
 *
 * In this example, we will re-implement the built-in pivot table command
 *
 * We will name our extension "mysheet", so our shared library will be named
 * zsvextmysheet.so (non-win) or zsvextmysheet.dll (win). After the shared lib is
 * built, place it anywhere in the PATH or in the same folder as the zsv binary.
 * Our extension can then be invoked by first running `sheet`, and then pressing
 * 'z'
 *
 * in addition, a description of our extension is displayed in the built-in help
 * command (?)
 *
 */

/**
 * *Required*: define our extension id, of up to 8 bytes in length
 */
const char *zsv_ext_id(void) {
  return "mysheet";
}

/**
 * When our library is initialized, zsv will pass it the address of the zsvlib
 * functions we will be using. We can keep track of this any way we want;
 * in this example, we use a global variable to store the function pointers
 */
static struct zsv_ext_callbacks zsv_cb;

#include "../sql_internal.h"

struct pivot_row {
  char *value; // to do: this will be the drill-down criteria
};

struct pivot_data {
  char *value_sql; // the sql expression entered by the user e.g. City
  char *data_filename;
  struct {
    struct pivot_row *data; // for each row, the value of the sql expression e.g. New York
    size_t capacity;
    size_t used;
  } rows;
};

static void pivot_data_delete(void *h) {
  struct pivot_data *pd = h;
  if (pd) {
    for (size_t i = 0; i < pd->rows.used; i++)
      free(pd->rows.data[i].value);
    free(pd->rows.data);
    free(pd->value_sql);
    free(pd->data_filename);
    free(pd);
  }
}

static struct pivot_data *pivot_data_new(const char *data_filename, const char *value_sql) {
  struct pivot_data *pd = calloc(1, sizeof(*pd));
  if (pd && (pd->value_sql = strdup(value_sql)) && (pd->data_filename = strdup(data_filename)))
    return pd;
  pivot_data_delete(pd);
  return NULL;
}

#define ZSV_MYSHEET_EXTENSION_PIVOT_DATA_ROWS_INITIAL 32
static int pivot_data_grow(struct pivot_data *pd) {
  if (pd->rows.used == pd->rows.capacity) {
    size_t new_capacity =
      pd->rows.capacity == 0 ? ZSV_MYSHEET_EXTENSION_PIVOT_DATA_ROWS_INITIAL : pd->rows.capacity * 2;
    struct pivot_row *new_data = realloc(pd->rows.data, new_capacity * sizeof(*pd->rows.data));
    if (!new_data)
      return ENOMEM;
    pd->rows.data = new_data;
    pd->rows.capacity = new_capacity;
  }
  return 0;
}

static int add_pivot_row(struct pivot_data *pd, const char *value, size_t len) {
  int err = pivot_data_grow(pd);
  char *value_dup = NULL;
  if (!err && value && len) {
    value_dup = malloc(len + 1);
    if (value_dup) {
      memcpy(value_dup, value, len);
      value_dup[len] = '\0';
    }
  }
  pd->rows.data[pd->rows.used++].value = value_dup;
  return err;
}

static struct pivot_row *get_pivot_row_data(struct pivot_data *pd, size_t row_ix) {
  if (pd && row_ix < pd->rows.used)
    return &pd->rows.data[row_ix];
  return NULL;
}

// TO DO: return zsvsheet_status
static enum zsv_ext_status get_cell_attrs(void *pdh, zsvsheet_cell_attr_t *attrs, size_t start_row, size_t row_count,
                                          size_t cols) {
  struct pivot_data *pd = pdh;
  size_t end_row = start_row + row_count;
  if (end_row > pd->rows.used)
    end_row = pd->rows.used;
  for (size_t i = start_row; i < end_row; i++)
    attrs[i * cols] = zsv_cb.ext_sheet_cell_profile_attrs(zsvsheet_cell_attr_profile_link);
  return zsv_ext_status_ok;
}

static void pivot_on_header_cell(void *ctx, size_t col_ix, const char *colname) {
  if (col_ix == 0) {
    add_pivot_row(ctx, NULL, 0);
  }
}

static void pivot_on_data_cell(void *ctx, size_t col_ix, const char *text, size_t len) {
  if (col_ix == 0)
    add_pivot_row(ctx, text, len);
}

static zsvsheet_status zsv_sqlite3_to_csv(zsvsheet_proc_context_t pctx, struct zsv_sqlite3_db *zdb, const char *sql,
                                          void *ctx, void (*on_header_cell)(void *, size_t, const char *),
                                          void (*on_data_cell)(void *, size_t, const char *, size_t len)) {
  const char *err_msg = NULL;
  zsvsheet_status zst = zsvsheet_status_error;
  sqlite3_stmt *stmt = NULL;
  if ((zdb->rc = sqlite3_prepare_v2(zdb->db, sql, -1, &stmt, NULL)) == SQLITE_OK) {
    char *tmp_fn = zsv_get_temp_filename("zsv_mysheet_ext_XXXXXXXX");
    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
    zsv_csv_writer cw = NULL;
    if (!tmp_fn)
      zst = zsvsheet_status_memory;
    else if (!(writer_opts.stream = fopen(tmp_fn, "wb"))) {
      zst = zsvsheet_status_error;
      err_msg = strerror(errno);
    } else if (!(cw = zsv_writer_new(&writer_opts)))
      zst = zsvsheet_status_memory;
    else {
      zst = zsvsheet_status_ok;
      unsigned char cw_buff[1024];
      zsv_writer_set_temp_buff(cw, cw_buff, sizeof(cw_buff));

      int col_count = sqlite3_column_count(stmt);
      // write header row
      for (int i = 0; i < col_count; i++) {
        const char *colname = sqlite3_column_name(stmt, i);
        zsv_writer_cell(cw, !i, (const unsigned char *)colname, colname ? strlen(colname) : 0, 1);
        if (on_header_cell)
          on_header_cell(ctx, i, colname);
      }

      // write sql results
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int i = 0; i < col_count; i++) {
          const unsigned char *text = sqlite3_column_text(stmt, i);
          int len = text ? sqlite3_column_bytes(stmt, i) : 0;
          zsv_writer_cell(cw, !i, text, len, 1);
          if (on_data_cell)
            on_data_cell(ctx, i, text, len);
        }
      }
    }
    if (cw)
      zsv_writer_delete(cw);
    if (writer_opts.stream)
      fclose(writer_opts.stream);

    if (tmp_fn && zsv_file_exists(tmp_fn))
      zst = zsv_cb.ext_sheet_open_file(pctx, tmp_fn, NULL);
    else {
      if (zst == zsvsheet_status_ok) {
        zst = zsvsheet_status_error; // to do: make this more specific
        if (!err_msg && zdb && zdb->rc != SQLITE_OK)
          err_msg = sqlite3_errmsg(zdb->db);
      }
    }
    free(tmp_fn);
  }
  if (stmt)
    sqlite3_finalize(stmt);
  if (err_msg)
    zsv_cb.ext_sheet_set_status(ctx, "Error: %s", err_msg);
  return zst;
}

//
zsvsheet_status pivot_drill_down(zsvsheet_proc_context_t ctx) {
  enum zsvsheet_status zst = zsvsheet_status_ok;
  char result_buffer[256] = {0};
  zsvsheet_buffer_t buff = zsv_cb.ext_sheet_buffer_current(ctx);
  struct pivot_data *pd;
  struct zsvsheet_rowcol rc;
  if (zsv_cb.ext_sheet_buffer_get_ctx(buff, (void **)&pd) != zsv_ext_status_ok ||
      zsv_cb.ext_sheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok) {
    return zsvsheet_status_error;
  }
  struct pivot_row *pr = get_pivot_row_data(pd, rc.row);
  if (pd && pd->data_filename && pd->value_sql && pr) {
    // zsv_cb.ext_sheet_prompt(ctx, result_buffer, sizeof(result_buffer), "You are in pivot_drill_down! row = %zu, value
    // = %s\n", rc.row, pr->value ? pr->value : "(null)");

    struct zsv_sqlite3_dbopts dbopts = {0};
    sqlite3_str *sql_str = NULL;
    struct zsv_sqlite3_db *zdb = zsv_cb.ext_sqlite3_db_new(&dbopts);
    if (!zdb || !(sql_str = sqlite3_str_new(zdb->db)))
      zst = zsvsheet_status_memory;
    else if (zdb->rc == SQLITE_OK && zsv_cb.ext_sqlite3_add_csv(zdb, pd->data_filename, NULL, NULL) == SQLITE_OK) {
      if (zsv_cb.ext_sheet_buffer_info(buff).has_row_num)
        sqlite3_str_appendf(sql_str, "select *");
      else
        sqlite3_str_appendf(sql_str, "select rowid as [Row #], *");
      sqlite3_str_appendf(sql_str, " from data where %s = %Q", pd->value_sql, pr->value);
      fprintf(stderr, "SQL: %s\n", sqlite3_str_value(sql_str));
      zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), NULL, NULL, NULL);
    }
    if (sql_str)
      sqlite3_free(sqlite3_str_finish(sql_str));
    if (zdb) {
      if (zst != zsvsheet_status_ok) {
        // to do: consolidate this with same code in sql.c
        if (zdb->err_msg)
          fprintf(stderr, "Error: %s\n", zdb->err_msg);
        else if (!zdb->db)
          fprintf(stderr, "Error (unable to open db, code %i): %s\n", zdb->rc, sqlite3_errstr(zdb->rc));
        else if (zdb->rc != SQLITE_OK)
          fprintf(stderr, "Error (code %i): %s\n", zdb->rc, sqlite3_errstr(zdb->rc));
      }
      zsv_cb.ext_sqlite3_db_delete(zdb);
    }
  }
  return zst;
}

/**
 * Here we define a custom command for the zsv `sheet` feature
 */
zsvsheet_status my_pivot_table_command_handler(zsvsheet_proc_context_t ctx) {
  char result_buffer[256] = {0};
  int ch = zsv_cb.ext_sheet_keypress(ctx);
  if (ch < 0)
    return zsvsheet_status_error;
  zsvsheet_buffer_t buff = zsv_cb.ext_sheet_buffer_current(ctx);
  const char *data_filename = NULL;
  if (buff)
    data_filename = zsv_cb.ext_sheet_buffer_data_filename(buff);
  if (!data_filename) { // TO DO: check that the underlying data is a tabular file and we know how to parse
    zsv_cb.ext_sheet_set_status(ctx, "Pivot table only available for tabular data buffers");
    return zsvsheet_status_ok;
  }
  struct zsv_opts opts = zsv_cb.ext_sheet_buffer_get_zsv_opts(buff);
  zsv_cb.ext_sheet_prompt(ctx, result_buffer, sizeof(result_buffer), "Pivot table: Enter group-by SQL expr");
  if (*result_buffer == '\0')
    return zsvsheet_status_ok;

  enum zsvsheet_status zst = zsvsheet_status_ok;
  struct zsv_sqlite3_dbopts dbopts = {0};
  struct zsv_sqlite3_db *zdb = zsv_cb.ext_sqlite3_db_new(&dbopts);
  sqlite3_str *sql_str = NULL;
  struct pivot_data *pd = NULL;
  if (!zdb || !(sql_str = sqlite3_str_new(zdb->db)))
    zst = zsvsheet_status_memory;
  else if (zdb->rc == SQLITE_OK && zsv_cb.ext_sqlite3_add_csv(zdb, data_filename, NULL, NULL) == SQLITE_OK) {
    sqlite3_str_appendf(sql_str, "select %s as value, count(1) as Count from data group by %s", result_buffer,
                        result_buffer);
    if (!(pd = pivot_data_new(data_filename, result_buffer)))
      zst = zsvsheet_status_memory;
    else {
      zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), pd, pivot_on_header_cell, pivot_on_data_cell);
      if (zst == zsvsheet_status_ok) {
        zsvsheet_buffer_t buff = zsv_cb.ext_sheet_buffer_current(ctx);
        zsv_cb.ext_sheet_buffer_set_ctx(buff, pd, pivot_data_delete);
        zsv_cb.ext_sheet_buffer_set_cell_attrs(buff, get_cell_attrs);
        zsv_cb.ext_sheet_buffer_on_newline(buff, pivot_drill_down);
        pd = NULL; // so that it isn't cleaned up below
      }
    }
    // TO DO: add param to ext_sheet_open_file to set filename vs data_filename, and set buffer type or proc owner
    // TO DO: add way to attach custom context, and custom context destructor, to the new buffer
    // TO DO: add cell highlighting
    // TO DO: add drill-down
  }

out:
  zsv_cb.ext_sqlite3_db_delete(zdb);
  if (sql_str)
    sqlite3_free(sqlite3_str_finish(sql_str));
  pivot_data_delete(pd);
  return zst;
}

/**
 * *Required*. Initialization is called when our extension is loaded
 * See my_extension.c for details
 */

enum zsv_ext_status zsv_ext_init(struct zsv_ext_callbacks *cb, zsv_execution_context ctx) {
  zsv_cb = *cb;
  zsv_cb.ext_set_help(ctx, "Sample zsv sheet extension");
  zsv_cb.ext_set_license(ctx,
                         "Unlicense. See https://github.com/spdx/license-list-data/blob/master/text/Unlicense.txt");
  const char *third_party_licenses[] = {"If we used any third-party software, we would list each license here", NULL};
  zsv_cb.ext_set_thirdparty(ctx, third_party_licenses);
  int proc_id = zsv_cb.ext_sheet_register_proc("my-sheet-pivot", "my sheet pivot", my_pivot_table_command_handler);
  if (proc_id < 0)
    return zsv_ext_status_error;
  zsv_cb.ext_sheet_register_proc_key_binding('v', proc_id);
  return zsv_ext_status_ok;
}

/**
 * exit: called once by zsv before the library is unloaded, if `zsv_ext_init()` was
 * previously called
 */
enum zsv_ext_status zsv_ext_exit(void) {
  fprintf(stderr, "Exiting mysheet extension example!\n");
  return zsv_ext_status_ok;
}
