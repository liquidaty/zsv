static zsvsheet_status zsv_sqlite3_to_csv(zsvsheet_proc_context_t pctx, struct zsv_sqlite3_db *zdb, const char *sql,
                                          const char **err_msg, void *ctx,
                                          void (*on_header_cell)(void *, size_t, const char *),
                                          void (*on_data_cell)(void *, size_t, const char *, size_t len)) {
  zsvsheet_status zst = zsvsheet_status_error;
  sqlite3_stmt *stmt = NULL;
  char have_data = 0;
  if ((zdb->rc = sqlite3_prepare_v2(zdb->db, sql, -1, &stmt, NULL)) == SQLITE_OK) {
    char *tmp_fn = zsv_get_temp_filename("zsv_mysheet_ext_XXXXXXXX");
    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
    zsv_csv_writer cw = NULL;
    if (!tmp_fn)
      zst = zsvsheet_status_memory;
    else if (!(writer_opts.stream = fopen(tmp_fn, "wb"))) {
      zst = zsvsheet_status_error;
      *err_msg = strerror(errno);
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
          have_data = 1;
          if (on_data_cell)
            on_data_cell(ctx, i, (const char *)text, len);
        }
      }
    }
    if (cw)
      zsv_writer_delete(cw);
    if (writer_opts.stream)
      fclose(writer_opts.stream);

    if (tmp_fn && zsv_file_exists(tmp_fn) && have_data) {
      struct zsvsheet_ui_buffer_opts uibopts = {0};
      uibopts.data_filename = tmp_fn;
      zst = zsvsheet_open_file_opts(pctx, &uibopts);
    } else {
      if (zst == zsvsheet_status_ok) {
        if (!have_data)
          zst = zsvsheet_status_no_data;
        if (!*err_msg && zdb && zdb->rc != SQLITE_OK) {
          zst = zsvsheet_status_error; // to do: make this more specific
          *err_msg = sqlite3_errmsg(zdb->db);
        }
      }
      if (tmp_fn && zsv_file_exists(tmp_fn))
        unlink(tmp_fn);
    }
    free(tmp_fn);
  }
  if (stmt)
    sqlite3_finalize(stmt);

  return zst;
}

static int is_constant_expression(sqlite3 *db, const char *expr, int *err) {
  sqlite3_stmt *stmt = NULL;
  // We try to prepare "SELECT [expr]". If this succeeds, the expression
  // does not depend on any columns and is therefore constant.
  char *sql_const_test = sqlite3_mprintf("SELECT %s", expr);
  if (!sql_const_test) {
    *err = errno;
    return 0;
  }

  int rc = sqlite3_prepare_v2(db, sql_const_test, -1, &stmt, NULL);
  if (stmt) {
    sqlite3_finalize(stmt);
    stmt = NULL;
  }
  sqlite3_free(sql_const_test);

  int is_constant = (rc == SQLITE_OK);
  return is_constant;
}

enum check_select_expression_result {
  zsv_select_sql_expression_valid = 0,
  zsv_select_sql_expression_invalid,
  zsv_select_sql_expression_multiple_statements,
  zsv_select_sql_expression_multiple_expressions,
  zsv_select_sql_expression_other
};

const char *check_select_expression_result_str(enum check_select_expression_result rc) {
  switch (rc) {
  case zsv_select_sql_expression_valid:
    return NULL;
  case zsv_select_sql_expression_invalid:
    return "Invalid SQL";
  case zsv_select_sql_expression_multiple_statements:
    return "Please enter only a single expression";
  case zsv_select_sql_expression_multiple_expressions:
    return "Please enter only a single expression";
  case zsv_select_sql_expression_other:
    return "Unknown error";
  }
  return NULL;
}

static int is_str_empty(const char *s) {
  if (!s)
    return 1;
  while (*s) {
    if (!isspace((unsigned char)*s)) {
      return 0;
    }
    s++;
  }
  return 1;
}

static enum check_select_expression_result check_select_expression(sqlite3 *db, const char *expr, int *err) {
  sqlite3_stmt *stmt = NULL;

  // Prepare "SELECT [expr] FROM data" to see if it's valid in the
  // context of the 'data' table.
  char *sql_valid_test = sqlite3_mprintf("SELECT %s FROM data LIMIT 0", expr);
  if (!sql_valid_test) {
    *err = errno;
    return zsv_select_sql_expression_other;
  }

  const char *pzTail = NULL;
  int rc = sqlite3_prepare_v2(db, sql_valid_test, -1, &stmt, &pzTail);
  sqlite3_free(sql_valid_test); // Free the string immediately
  if (rc != SQLITE_OK) {
    if (stmt)
      sqlite3_finalize(stmt);
    return zsv_select_sql_expression_invalid;
  }

  // if we are here, the expression is valid. check it is a single statement
  // pzTail points to the start of the *next* statement. If it's not
  // empty or just whitespace, the user tried to inject a second command.
  if (!is_str_empty(pzTail)) {
    sqlite3_finalize(stmt);
    return zsv_select_sql_expression_multiple_statements;
  }

  // Check that we have a single expression (e.g., not "myvalue, 123")
  int col_count = sqlite3_column_count(stmt);

  sqlite3_finalize(stmt);
  if (col_count != 1)
    return zsv_select_sql_expression_multiple_expressions;

  return zsv_select_sql_expression_valid;
}
