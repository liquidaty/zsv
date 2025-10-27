/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <assert.h>
#include <errno.h>
#include "../external/sqlite3/sqlite3.h"
#include <zsv/ext/implementation.h>
#include <zsv/ext/sheet.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/prop.h>
#include "file.h"
#include "handlers_internal.h"
#include "./curses.h"
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

#define ZSV_MYSHEET_PIVOT_DATA_ROWS_INITIAL 32
static int pivot_data_grow(struct pivot_data *pd) {
  if (pd->rows.used == pd->rows.capacity) {
    size_t new_capacity = pd->rows.capacity == 0 ? ZSV_MYSHEET_PIVOT_DATA_ROWS_INITIAL : pd->rows.capacity * 2;
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
static enum zsv_ext_status get_cell_attrs(void *pdh, zsvsheet_cell_attr_t *attrs, size_t start_row, size_t row_count, size_t cols) {
  struct pivot_data *pd = pdh;
  size_t end_row = start_row + row_count;
  int attr = 0;

#ifdef A_BOLD
  attr |= A_BOLD;
#endif
// Absent on Mac OSX 13
#ifdef A_ITALIC
  attr |= A_ITALIC;
#endif

  if (end_row > pd->rows.used)
    end_row = pd->rows.used;
  for (size_t i = start_row; i < end_row; i++)
    attrs[i * cols] = attr;
  return zsv_ext_status_ok;
}

static void pivot_on_header_cell(void *ctx, size_t col_ix, const char *colname) {
  (void)colname;
  if (col_ix == 0)
    add_pivot_row(ctx, NULL, 0);
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
            on_data_cell(ctx, i, (const char *)text, len);
        }
      }
    }
    if (cw)
      zsv_writer_delete(cw);
    if (writer_opts.stream)
      fclose(writer_opts.stream);

    if (tmp_fn && zsv_file_exists(tmp_fn)) {
      struct zsvsheet_ui_buffer_opts uibopts = {0};
      uibopts.data_filename = tmp_fn;
      zst = zsvsheet_open_file_opts(pctx, &uibopts);
    } else {
      if (zst == zsvsheet_status_ok) {
        zst = zsvsheet_status_error; // to do: make this more specific
        if (!err_msg && zdb && zdb->rc != SQLITE_OK)
          err_msg = sqlite3_errmsg(zdb->db);
      }
    }
    if (zst != zsvsheet_status_ok)
      free(tmp_fn);
  }
  if (stmt)
    sqlite3_finalize(stmt);
  if (err_msg)
    zsvsheet_set_status(ctx, "Error: %s", err_msg);
  return zst;
}

zsvsheet_status pivot_drill_down(zsvsheet_proc_context_t ctx) {
  enum zsvsheet_status zst = zsvsheet_status_ok;
  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  struct pivot_data *pd;
  struct zsvsheet_rowcol rc;
  if (zsvsheet_buffer_get_ctx(buff, (void **)&pd) != zsv_ext_status_ok ||
      zsvsheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok) {
    return zsvsheet_status_error;
  }
  struct pivot_row *pr = get_pivot_row_data(pd, rc.row);
  if (pd && pd->data_filename && pd->value_sql && pr) {
    struct zsv_sqlite3_dbopts dbopts = {0};
    sqlite3_str *sql_str = NULL;
    struct zsv_sqlite3_db *zdb = zsv_sqlite3_db_new(&dbopts);

    if (!zdb || !(sql_str = sqlite3_str_new(zdb->db)))
      zst = zsvsheet_status_memory;
    else if (zdb->rc == SQLITE_OK && zsv_sqlite3_add_csv(zdb, pd->data_filename, NULL, NULL) == SQLITE_OK) {
      if (zsvsheet_buffer_info(buff).has_row_num)
        sqlite3_str_appendf(sql_str, "select *");
      else
        sqlite3_str_appendf(sql_str, "select rowid as [Row #], *");
      sqlite3_str_appendf(sql_str, " from data where \"%w\" = %Q", pd->value_sql, pr->value);
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
      zsv_sqlite3_db_delete(zdb);
    }
  }
  return zst;
}

/**
 * Here we define a custom command for the zsv `sheet` feature
 */
static zsvsheet_status zsvsheet_pivot_handler(struct zsvsheet_proc_context *ctx) {
  char result_buffer[256] = {0};
  const char *expr;
  struct zsvsheet_rowcol rc;
  int ch = zsvsheet_ext_keypress(ctx);
  if (ch < 0)
    return zsvsheet_status_error;

  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  const char *data_filename = NULL;
  if (buff)
    data_filename = zsvsheet_buffer_data_filename(buff);

  if (!data_filename) { // TO DO: check that the underlying data is a tabular file and we know how to parse
    zsvsheet_set_status(ctx, "Pivot table only available for tabular data buffers");
    return zsvsheet_status_ok;
  }

  switch (ctx->proc_id) {
  case zsvsheet_builtin_proc_pivot_expr:
    zsvsheet_ext_prompt(ctx, result_buffer, sizeof(result_buffer), "Pivot table: Enter group-by SQL expr");
    if (*result_buffer == '\0')
      return zsvsheet_status_ok;
    expr = result_buffer;
    break;
  case zsvsheet_builtin_proc_pivot_cur_col:
    if (zsvsheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok)
      return zsvsheet_status_error;
    expr = zsvsheet_ui_buffer_get_header(buff, rc.col);
    assert(expr);
    break;
  default:
    assert(0);
    return zsvsheet_status_error;
  }

  enum zsvsheet_status zst = zsvsheet_status_ok;
  struct zsv_sqlite3_dbopts dbopts = {0};
  struct zsv_opts zopts = zsvsheet_buffer_get_zsv_opts(buff);
  struct zsv_sqlite3_db *zdb = zsv_sqlite3_db_new(&dbopts);
  sqlite3_str *sql_str = NULL;
  struct pivot_data *pd = NULL;
  if (!zdb || !(sql_str = sqlite3_str_new(zdb->db)))
    zst = zsvsheet_status_memory;
  else if (zdb->rc == SQLITE_OK && zsv_sqlite3_add_csv(zdb, data_filename, &zopts, NULL) == SQLITE_OK) {
    sqlite3_str_appendf(sql_str, "select \"%w\" as value, count(1) as Count from data group by \"%w\"", expr, expr);
    if (!(pd = pivot_data_new(data_filename, expr)))
      zst = zsvsheet_status_memory;
    else {
      zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), pd, pivot_on_header_cell, pivot_on_data_cell);
      if (zst == zsvsheet_status_ok) {
        buff = zsvsheet_buffer_current(ctx);
        zsvsheet_buffer_set_ctx(buff, pd, pivot_data_delete);
        zsvsheet_buffer_set_cell_attrs(buff, get_cell_attrs);
        zsvsheet_buffer_on_newline(buff, pivot_drill_down);
        pd = NULL; // so that it isn't cleaned up below
      }
    }
    // TO DO: add param to ext_sheet_open_file to set filename vs data_filename, and set buffer type or proc owner
    // TO DO: add way to attach custom context, and custom context destructor, to the new buffer
    // TO DO: add cell highlighting
  }

  zsv_sqlite3_db_delete(zdb);
  if (sql_str)
    sqlite3_free(sqlite3_str_finish(sql_str));
  pivot_data_delete(pd);
  return zst;
}
