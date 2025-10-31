/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights
 * reserved.  This file is part of zsv/lib, distributed under the
 * license defined at https://opensource.org/licenses/MIT
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

struct sqlfilter_data {
  char *value_sql; // the sql expression entered by the user e.g. City
  char *data_filename;
};

static void sqlfilter_data_delete(void *h) {
  struct sqlfilter_data *pd = h;
  if (pd) {
    free(pd->value_sql);
    free(pd->data_filename);
    free(pd);
  }
}

static struct sqlfilter_data *sqlfilter_data_new(const char *data_filename, const char *value_sql) {
  struct sqlfilter_data *pd = calloc(1, sizeof(*pd));
  if (pd && (pd->value_sql = strdup(value_sql)) && (pd->data_filename = strdup(data_filename)))
    return pd;
  sqlfilter_data_delete(pd);
  return NULL;
}

static void zsvsheet_check_buffer_worker_updates(struct zsvsheet_ui_buffer *ub,
                                                 struct zsvsheet_display_dimensions *display_dims,
                                                 struct zsvsheet_sheet_context *handler_state);

/**
 * Here we define a custom command for the zsv `sheet` feature
 */
static zsvsheet_status zsvsheet_sqlfilter_handler(struct zsvsheet_proc_context *ctx) {
  char result_buffer[256] = {0};
  const char *expr;
  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  struct zsvsheet_buffer_data bd = zsvsheet_buffer_info(buff);
  const char add_row_num = !bd.has_row_num;
  const char *data_filename = NULL;
  if (buff)
    data_filename = zsvsheet_buffer_data_filename(buff);

  if (!data_filename) { // TO DO: check that the underlying data is a tabular file and we know how to parse
    zsvsheet_ui_buffer_set_status(buff, "SQL filter only available for tabular data buffers");
    return zsvsheet_status_ok;
  }

  char *selected_cell_str_dup = NULL;
  switch (ctx->proc_id) {
  case zsvsheet_builtin_proc_sqlfilter:
    zsvsheet_ext_prompt(ctx, result_buffer, sizeof(result_buffer), "Enter SQL expr to filter by");
    if (*result_buffer == '\0')
      return zsvsheet_status_ok;
    expr = result_buffer;
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
  struct sqlfilter_data *sqlfd = NULL;
  if (!zdb || !(sql_str = sqlite3_str_new(zdb->db)))
    zst = zsvsheet_status_memory;
  else if (zdb->rc == SQLITE_OK && zsv_sqlite3_add_csv_no_dq(zdb, data_filename, &zopts, NULL) == SQLITE_OK) {
    int ok = 0;
    const char *err_msg = NULL;
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
    } else {
      if (add_row_num)
        sqlite3_str_appendf(sql_str, "select ROWID as %Q, * from data where %s", ZSVSHEET_ROWNUM_HEADER, expr);
      else
        sqlite3_str_appendf(sql_str, "select * from data where %s", expr);

      if (!(sqlfd = sqlfilter_data_new(data_filename, expr)))
        zst = zsvsheet_status_memory;
      else {
        zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), &err_msg, sqlfd, NULL, NULL);
        if (zst != zsvsheet_status_ok) {
          if (zst == zsvsheet_status_no_data)
            zsvsheet_ui_buffer_set_status(buff, "No results returned");
          else
            zsvsheet_ui_buffer_set_status(buff, err_msg ? err_msg : "Unexpected error preparing SQL");
        } else {
          buff = zsvsheet_buffer_current(ctx);
          zsvsheet_buffer_set_ctx(buff, sqlfd, sqlfilter_data_delete);
          sqlfd = NULL; // so that it isn't cleaned up below

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
  sqlfilter_data_delete(sqlfd);
  free(selected_cell_str_dup);
  return zst;
}
