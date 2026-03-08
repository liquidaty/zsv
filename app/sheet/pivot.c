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

#include "sheet-sql.c"

struct pivot_row {
  char *value;
};

struct pivot_data {
  char *value_sql; // the sql expression entered by the user e.g. City
  char *data_filename;
  struct {
    struct pivot_row *data; // for each row, the value of the sql expression e.g. New York
    size_t capacity;
    size_t used;
  } rows;
  char column_name_expr;
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

static struct pivot_data *pivot_data_new(const char *data_filename, const char *value_sql, char column_name_expr) {
  struct pivot_data *pd = calloc(1, sizeof(*pd));
  if (pd && (pd->value_sql = strdup(value_sql)) && (pd->data_filename = strdup(data_filename))) {
    pd->column_name_expr = column_name_expr;
    return pd;
  }
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

static enum zsv_ext_status get_cell_attrs(void *pdh, zsvsheet_cell_attr_t *attrs, size_t start_row, size_t row_count,
                                          size_t cols) {
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
    else if (zdb->rc == SQLITE_OK && zsv_sqlite3_add_csv_no_dq(zdb, pd->data_filename, NULL, NULL) == SQLITE_OK) {
      if (zsvsheet_buffer_info(buff).has_row_num)
        sqlite3_str_appendf(sql_str, "select *");
      else
        sqlite3_str_appendf(sql_str, "select rowid as [Row #], *");
      if (pd->column_name_expr)
        sqlite3_str_appendf(sql_str, " from data where \"%w\" = %Q", pd->value_sql, pr->value);
      else
        // we haven't tracked whether the underlying value is a string, number or other
        // we only know that its string representation is pr->value
        // so we add `|| ''` to the sql expression to force text conversion
        sqlite3_str_appendf(sql_str, " from data where (%s) || '' = %Q", pd->value_sql, pr->value);
      const char *err_msg = NULL;
      zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), &err_msg, NULL, NULL, NULL);
      if (err_msg)
        zsvsheet_ui_buffer_set_status(buff, err_msg);
      else if (zst == zsvsheet_status_no_data)
        zsvsheet_ui_buffer_set_status(buff, "No results returned");
    }

    if (sql_str)
      sqlite3_free(sqlite3_str_finish(sql_str));
    if (zdb) {
      if (zst != zsvsheet_status_ok) {
        // to do: consolidate this with same code in /app/sql.c
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

static void zsvsheet_check_buffer_worker_updates(struct zsvsheet_ui_buffer *ub,
                                                 struct zsvsheet_display_dimensions *display_dims,
                                                 struct zsvsheet_sheet_context *handler_state);

/**
 * Here we define a custom command for the zsv `sheet` feature
 */
static zsvsheet_status zsvsheet_pivot_handler(struct zsvsheet_proc_context *ctx) {
  char result_buffer[256] = {0};
  const char *expr;
  char column_name_expr;
  struct zsvsheet_rowcol rc;
  int ch = zsvsheet_ext_keypress(ctx);

  if (ch < 0)
    return zsvsheet_status_error;

  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  const char *data_filename = NULL;
  if (buff)
    data_filename = zsvsheet_buffer_data_filename(buff);

  if (!data_filename) { // TO DO: check that the underlying data is a tabular file and we know how to parse
    zsvsheet_ui_buffer_set_status(buff, "Pivot table only available for tabular data buffers");
    return zsvsheet_status_ok;
  }

  char *selected_cell_str_dup = NULL;
  switch (ctx->proc_id) {
  case zsvsheet_builtin_proc_pivot_expr:
    zsvsheet_ext_prompt(ctx, result_buffer, sizeof(result_buffer), "Pivot table: Enter group-by SQL expr");
    if (*result_buffer == '\0')
      return zsvsheet_status_ok;
    expr = result_buffer;
    column_name_expr = 0;
    break;
  case zsvsheet_builtin_proc_pivot_cur_col:
    if (zsvsheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok)
      return zsvsheet_status_error;
    const unsigned char *selected_cell_str =
      zsvsheet_screen_buffer_cell_display(((struct zsvsheet_ui_buffer *)buff)->buffer, rc.row, rc.col);
    while (selected_cell_str && *selected_cell_str == ' ')
      selected_cell_str++;
    size_t len = selected_cell_str ? strlen((const char *)selected_cell_str) : 0;
    while (len > 0 && selected_cell_str[len - 1] == ' ')
      len--;
    if (len)
      selected_cell_str_dup = zsv_memdup(selected_cell_str, len);
    expr = zsvsheet_ui_buffer_get_header(buff, rc.col);
    column_name_expr = 1;
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
  else if (zdb->rc == SQLITE_OK && zsv_sqlite3_add_csv_no_dq(zdb, data_filename, &zopts, NULL) == SQLITE_OK) {
    int ok = 0;
    const char *err_msg = NULL;
    if (column_name_expr) {
      sqlite3_str_appendf(sql_str, "select \"%w\" as %#Q, count(1) as Count from data group by \"%w\"", expr, expr,
                          expr);
      ok = 1;
    } else {
      int err = 0;
      if (is_constant_expression(zdb->db, expr, &err))
        err_msg = "Please enter an expression that is not a constant";
      else {
        enum check_select_expression_result expr_rc = check_select_expression(zdb->db, expr, &err);
        if (expr_rc != zsv_select_sql_expression_valid)
          err_msg = check_select_expression_result_str(expr_rc);
        else if (!err)
          ok = 1;
      }
      if (!ok) {
        if (err)
          zsvsheet_ui_buffer_set_status(buff, strerror(err));
        else if (err_msg)
          zsvsheet_ui_buffer_set_status(buff, err_msg);
        else
          zsvsheet_ui_buffer_set_status(buff, "Unknown error");
      } else
        sqlite3_str_appendf(sql_str, "select %s as %#Q, count(1) as Count from data group by %s", expr, expr, expr);
    }
    if (ok) {
      if (!(pd = pivot_data_new(data_filename, expr, column_name_expr)))
        zst = zsvsheet_status_memory;
      else {
        zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), &err_msg, pd, pivot_on_header_cell,
                                 pivot_on_data_cell);
        if (zst != zsvsheet_status_ok) {
          if (zst == zsvsheet_status_no_data)
            zsvsheet_ui_buffer_set_status(buff, "No results returned");
          else
            zsvsheet_ui_buffer_set_status(buff, err_msg ? err_msg : "Unexpected error preparing SQL");
        } else {
          buff = zsvsheet_buffer_current(ctx);
          zsvsheet_buffer_set_ctx(buff, pd, pivot_data_delete);
          zsvsheet_buffer_set_cell_attrs(buff, get_cell_attrs);
          zsvsheet_buffer_on_newline(buff, pivot_drill_down);
          pd = NULL; // so that it isn't cleaned up below

          while (!zsvsheet_ui_buffer_index_ready(buff, 0))
            napms(200); // sleep for 200ms, then check index again
          // TO DO: fix this if there is no data!

          if (selected_cell_str_dup) {
            struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
            struct zsvsheet_display_info *di = &state->display_info;
            zsvsheet_check_buffer_worker_updates(buff, di->dimensions, NULL);
            zsvsheet_handle_find_next(di, buff, selected_cell_str_dup,
                                      1, // find value in first column
                                      1, // exact
                                      1, // header_span
                                      di->dimensions, &di->update_buffer, NULL);
          }
        }
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
  free(selected_cell_str_dup);
  return zst;
}
