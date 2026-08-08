/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights
 * reserved.  This file is part of zsv/lib, distributed under the
 * license defined at https://opensource.org/licenses/MIT
 *
 * Included from sheet.c after sqlfilter.c; the static SQL helpers
 * (zsv_sqlite3_to_csv, is_constant_expression, check_select_expression,
 * zsvsheet_sql_rowid_ref) are visible because pivot.c includes sheet-sql.c
 * earlier in the same translation unit.
 */

/* Strip one trailing case-insensitive `asc`/`desc` word from expr (which must be
 * writable), preceded by whitespace or constituting the entire string. Returns
 * 'a'/'d' for the stripped word, else dflt. Trailing whitespace is removed either
 * way; the caller handles a resulting empty string. */
static char zsvsheet_sort_split_direction(char *expr, char dflt) {
  size_t len = strlen(expr);
  while (len && isspace((unsigned char)expr[len - 1]))
    expr[--len] = '\0';
  size_t cut = 0;
  char dir = 0;
  if (len >= 4 && !sqlite3_strnicmp(expr + len - 4, "desc", 4)) {
    cut = 4;
    dir = 'd';
  } else if (len >= 3 && !sqlite3_strnicmp(expr + len - 3, "asc", 3)) {
    cut = 3;
    dir = 'a';
  }
  if (!cut || (len > cut && !isspace((unsigned char)expr[len - cut - 1])))
    return dflt;
  len -= cut;
  expr[len] = '\0';
  while (len && isspace((unsigned char)expr[len - 1]))
    expr[--len] = '\0';
  return dir;
}

enum zsvsheet_sort_param_kind {
  zsvsheet_sort_param_column = 0,
  zsvsheet_sort_param_direction,
  zsvsheet_sort_param_flags
};

/* Pure string classification of one sort parameter. `asc`/`desc`
 * (case-insensitive) are reserved direction words, never column names; a
 * nonempty token of only [nfNF] characters is flag-shaped ('f' wins over 'n'
 * within a token); anything else is a column. Sets *dir or *flags only for the
 * kind returned. The flag-vs-column ambiguity re-test (a flag-shaped token
 * that names a real column) is the caller's job. */
static enum zsvsheet_sort_param_kind zsvsheet_sort_classify_param(const char *tok, char *dir, char *flags) {
  if (!sqlite3_stricmp(tok, "asc")) {
    *dir = 'a';
    return zsvsheet_sort_param_direction;
  }
  if (!sqlite3_stricmp(tok, "desc")) {
    *dir = 'd';
    return zsvsheet_sort_param_direction;
  }
  char f = 0;
  char flag_shaped = *tok != '\0';
  for (const char *p = tok; *p && flag_shaped; p++) {
    if (*p == 'f' || *p == 'F')
      f = 'f';
    else if (*p != 'n' && *p != 'N')
      flag_shaped = 0;
  }
  if (flag_shaped) {
    *flags = f ? 'f' : 'n';
    return zsvsheet_sort_param_flags;
  }
  return zsvsheet_sort_param_column;
}

/* Resolve the sort key against the result set of the same SELECT that will be
 * executed (prepared with ` limit 0`): with name == NULL, return the column name
 * at result ordinal col_ix (the cursor's screen ordinal, which the result set
 * mirrors by construction); with name given, return the canonical (deduped) name
 * matching it case-insensitively. On success *ordinal is set to the resolved
 * result ordinal (the caller needs it to tell the synthesized rowid alias apart
 * from a same-named data column). `rid` is the unshadowed rowid spelling from
 * zsvsheet_sql_rowid_ref(), so this prefix stays identical to the executed
 * SELECT. NULL on any failure or no match; caller frees. */
static char *zsvsheet_sort_resolve_key(struct zsv_sqlite3_db *zdb, char has_row_num, const char *rid, size_t col_ix,
                                       const char *name, size_t *ordinal) {
  char *result = NULL;
  sqlite3_stmt *stmt = NULL;
  char *sql = has_row_num ? sqlite3_mprintf("select * from data limit 0")
                          : sqlite3_mprintf("select %s as %Q, * from data limit 0", rid, ZSVSHEET_ROWNUM_HEADER);
  if (!sql)
    return NULL;
  if (sqlite3_prepare_v2(zdb->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    int col_count = sqlite3_column_count(stmt); // >= 0 per sqlite3 API
    if (name) {
      for (int i = 0; i < col_count && !result; i++) {
        const char *cn = sqlite3_column_name(stmt, i);
        if (cn && !sqlite3_stricmp(cn, name)) {
          result = strdup(cn); // NULL on alloc failure falls through to the caller's error path
          if (result)
            *ordinal = (size_t)i;
        }
      }
    } else if (col_ix < (size_t)col_count) {
      const char *cn = sqlite3_column_name(stmt, (int)col_ix); // in-bounds per the check above
      if (cn) {
        result = strdup(cn);
        if (result)
          *ordinal = col_ix;
      }
    }
  }
  if (stmt)
    sqlite3_finalize(stmt);
  sqlite3_free(sql);
  return result;
}

/**
 * Single handler for the `sort` (ascending, key 'o'), `sortdesc` (descending,
 * key 'O', alias `sort!`) and `sortexpr` (ORDER BY a SQL expression)
 * procedures. Default column-sort policy (documented in docs/sheet.md):
 * blank, whitespace-only and missing cells always last; numeric-looking cells
 * sort numerically as a group before text; everything else as text, NOCASE.
 * The `n` (leading-integer) and `f` (float) flags replace that policy, and
 * every shape carries a rowid tiebreak (the unshadowed spelling from
 * zsvsheet_sql_rowid_ref -- may be _ROWID_ or OID) so ties keep original
 * (or, on a derived buffer, previous-sort) order in both directions.
 */
static zsvsheet_status zsvsheet_sort_handler(struct zsvsheet_proc_context *ctx) {
  char expr_buffer[256] = {0};
  zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
  struct zsvsheet_buffer_data bd = zsvsheet_buffer_info(buff);
  const char add_row_num = !bd.has_row_num;
  const char *data_filename = NULL;
  if (buff)
    data_filename = zsvsheet_buffer_data_filename(buff);

  if (!data_filename) { // e.g. the help screen
    zsvsheet_ui_buffer_set_status(buff, "Sort only available for tabular data buffers");
    return zsvsheet_status_ok;
  }

  struct zsvsheet_buffer_info_internal binfo = zsvsheet_buffer_info_internal(buff);
  if (binfo.write_in_progress && !binfo.write_done)
    return zsvsheet_status_busy;

  char is_expr = 0;
  char dir = 'a';
  switch (ctx->proc_id) {
  case zsvsheet_builtin_proc_sort_cur_col:
    break;
  case zsvsheet_builtin_proc_sort_cur_col_desc:
    dir = 'd';
    break;
  case zsvsheet_builtin_proc_sort_expr:
    is_expr = 1;
    break;
  default:
    assert(0);
    return zsvsheet_status_error;
  }
  const char *sort_name = NULL; // :sort <col> params path; NULL = cursor path
  struct zsvsheet_rowcol rc = {0};

  if (is_expr) {
    if (ctx->num_params > 0) { // join params; SQL is whitespace-insensitive
      size_t pos = 0;
      for (int i = 0; i < ctx->num_params; i++) {
        const char *p = ctx->params[i].u.string;
        size_t len = strlen(p);
        // never silently truncate: a truncated expression can be valid-but-different SQL
        if (len >= sizeof(expr_buffer) - 1 - pos - (pos ? 1 : 0)) {
          zsvsheet_ui_buffer_set_status(buff, "Expression too long");
          return zsvsheet_status_ok;
        }
        if (pos)
          expr_buffer[pos++] = ' ';
        memcpy(expr_buffer + pos, p, len);
        pos += len;
      }
      expr_buffer[pos] = '\0';
    } else {
      if (!ctx->invocation.interactive)
        return zsvsheet_status_error;
      zsvsheet_ext_prompt(ctx, expr_buffer, sizeof(expr_buffer), "Sort by SQL expression [asc|desc]");
      if (*expr_buffer == '\0')
        return zsvsheet_status_ok;
    }
    dir = zsvsheet_sort_split_direction(expr_buffer, dir);
    if (*expr_buffer == '\0') {
      zsvsheet_ui_buffer_set_status(buff, "usage: sortexpr <SQL expression> [asc|desc]");
      return zsvsheet_status_ok;
    }
  }
  // Column-proc params are classified below, AFTER db creation: a flag-shaped
  // token is a column exactly when it resolves against the data, so the
  // classification needs the db. The sortexpr prompt above stays pre-db so no
  // SQLite connection is held across user interaction.

  enum zsvsheet_status zst = zsvsheet_status_ok;
  // interactive (no flag surface): disambiguate duplicate input columns (a,b,a -> a,b,a_2)
  struct zsv_sqlite3_dbopts dbopts = {.dedupe_cols = 1};
  struct zsv_opts zopts = zsvsheet_buffer_get_zsv_opts(buff);
  struct zsv_sqlite3_db *zdb = zsv_sqlite3_db_new(&dbopts);
  sqlite3_str *sql_str = NULL;
  char *key = NULL;       // resolved sort column name (column procs only)
  size_t key_ordinal = 0; // result ordinal of `key`, set with it by resolve_key
  char *status = NULL;    // asprintf'd so a long column name is never truncated
  char flags = 0;         // 0 = default policy; 'n' = leading integer; 'f' = float
  const char *err_msg = NULL;
  const char *rid = "ROWID"; // unshadowed rowid spelling, refreshed once the vtab exists

  if (!zdb || !(sql_str = sqlite3_str_new(zdb->db))) {
    zst = zsvsheet_status_memory;
    goto cleanup;
  }
  if (zdb->rc != SQLITE_OK || zsv_sqlite3_add_csv_no_dq(zdb, data_filename, &zopts, NULL) != SQLITE_OK) {
    zst = zsvsheet_status_error;
    zsvsheet_ui_buffer_set_status(buff, zdb->err_msg ? zdb->err_msg : "Unexpected error opening data file");
    goto cleanup;
  }
  rid = zsvsheet_sql_rowid_ref(zdb->db);

  if (is_expr) {
    // best-effort pre-check; whatever it misses (e.g. aggregates) is still
    // caught and reported via zsv_sqlite3_to_csv()'s err_msg below
    int expr_ok = 0;
    int err = 0;
    if (is_constant_expression(zdb->db, expr_buffer, &err))
      err_msg = "Please enter an expression that is not a constant";
    else {
      enum check_select_expression_result expr_rc = check_select_expression(zdb->db, expr_buffer, &err);
      if (expr_rc != zsv_select_sql_expression_valid)
        err_msg = check_select_expression_result_str(expr_rc);
      else if (!err)
        expr_ok = 1;
    }
    if (!expr_ok) {
      if (err)
        zsvsheet_ui_buffer_set_status(buff, strerror(err));
      else
        zsvsheet_ui_buffer_set_status(buff, err_msg ? err_msg : "Unknown error");
      goto cleanup; // an input error, not a failure: zst stays ok
    }
  } else {
    // grammar (total order, left to right): asc/desc are reserved direction
    // words, never columns (a column literally named `asc` is reachable only
    // via the cursor keys) and the last one wins; a [nfNF]+ token is a column
    // if the column slot is empty AND it names a real column (leftmost such
    // token takes the slot -- `:sort n n` is the column-then-flag escape
    // hatch), else flags with 'f' beating 'n'; any other token fills the
    // empty column slot or is a usage error. Column names containing spaces
    // need quotes, as with :filter.
    if (ctx->num_params > 3)
      goto usage;
    for (int i = 0; i < ctx->num_params; i++) {
      const char *tok = ctx->params[i].u.string;
      char tdir = 0, tflag = 0;
      switch (zsvsheet_sort_classify_param(tok, &tdir, &tflag)) {
      case zsvsheet_sort_param_direction:
        dir = tdir;
        break;
      case zsvsheet_sort_param_flags:
        if (!key && !sort_name) { // same slot test as the column case below: a
                                  // pending unresolved column must not be stolen
          size_t ord = 0;
          char *k = zsvsheet_sort_resolve_key(zdb, bd.has_row_num, rid, 0, tok, &ord);
          if (k) { // resolves: the token is that column, not flags
            key = k;
            sort_name = tok;
            key_ordinal = ord;
            break;
          }
        }
        if (flags != 'f')
          flags = tflag;
        break;
      case zsvsheet_sort_param_column:
        if (key || sort_name)
          goto usage; // column slot already taken
        sort_name = tok;
        break;
      }
    }
    if (!key) { // plain column param, or cursor path when no column given
      if (!sort_name && zsvsheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok) {
        zst = zsvsheet_status_error;
        goto cleanup;
      }
      if (!(key = zsvsheet_sort_resolve_key(zdb, bd.has_row_num, rid, rc.col, sort_name, &key_ordinal))) {
        if (sort_name && asprintf(&status, "Column not found: %s", sort_name) != -1)
          zsvsheet_ui_buffer_set_status(buff, status);
        else {
          status = NULL; // asprintf leaves its output indeterminate on failure
          zsvsheet_ui_buffer_set_status(buff, sort_name ? "Column not found" : "Unable to resolve sort column");
        }
        goto cleanup; // an input error, not a failure: zst stays ok
      }
    }
  }

  if (add_row_num) // preserve original row numbers in the sorted output
    sqlite3_str_appendf(sql_str, "select %s as %Q, * from data", rid, ZSVSHEET_ROWNUM_HEADER);
  else
    sqlite3_str_appendf(sql_str, "select * from data");
  {
    // Every shape below except `order by <rid>` ends with a `, <rid> asc`
    // tiebreak: SQLite's sorter is observed stable in every configuration
    // reachable from here, but stability is undocumented, and the tiebreak
    // turns it into a guarantee -- on a derived buffer the rowid is the
    // previous sort's row order, making chained sorts true secondary sorts.
    // `rid` is the rowid spelling no data column shadows, so the tiebreak is
    // positional even when a column named rowid exists; only the pathological
    // triple shadow (rowid AND _rowid_ AND oid columns) falls back to the
    // shadowed "ROWID" (deterministic, not positional).
    const char *dir_sql = dir == 'd' ? "desc" : "asc";
    if (is_expr) // the user asked for exactly this expression, so no grouping keys
      sqlite3_str_appendf(sql_str, " order by (%s) %s, %s asc", expr_buffer, dir_sql, rid);
    else if (add_row_num && key_ordinal == 0)
      // the key is the synthesized rowid alias. Referencing it by its alias
      // name inside an ORDER BY expression would bind to any TABLE column
      // matching that name case-insensitively (e.g. a `row #` data column),
      // silently sorting by the wrong column -- so order by the rowid itself,
      // which is also the intended "restore original order" semantics. The
      // rowid is unique, so a tiebreak would be dead code here
      sqlite3_str_appendf(sql_str, " order by %s %s", rid, dir_sql);
    else if (flags == 'f')
      // float: cast-only, no grouping -- cells without a leading decimal
      // float cast to 0.0 and sort inline at 0.0 (vim :sort f measured
      // behavior for ordinary decimal tokens; strtod extensions like 0x10,
      // inf and nan all cast to 0.0 here, a documented divergence)
      sqlite3_str_appendf(sql_str, " order by cast(trim(coalesce(\"%w\",'')) as real) %s, %s asc", key, dir_sql, rid);
    else if (flags == 'n')
      // leading integer: cells with no leading number first, in original
      // order (ties fall through to the rowid tiebreak), then by leading
      // integer value -- like vim :sort n except the number must start the
      // cell (embedded numbers are not extracted; documented divergence)
      sqlite3_str_appendf(sql_str,
                          " order by case when trim(coalesce(\"%w\",'')) glob '[0-9]*'"
                          " or trim(coalesce(\"%w\",'')) glob '[+-][0-9]*' then 1 else 0 end %s"
                          ", case when trim(coalesce(\"%w\",'')) glob '[0-9]*'"
                          " or trim(coalesce(\"%w\",'')) glob '[+-][0-9]*'"
                          " then cast(trim(coalesce(\"%w\",'')) as integer) end %s"
                          ", %s asc",
                          key, key, dir_sql, key, key, key, dir_sql, rid);
    else
      // key 1: blank/whitespace-only/missing always last, either direction
      // key 2: numeric-looking group (only numeric charset AND at least one
      //        digit, so 'ee', '-' or '+' are text) first; last when desc.
      //        Degenerate all-charset tokens like '1-2' still pass and cast
      //        by numeric prefix -- accepted approximation
      // key 3: numeric value, scoped so text rows all tie (NULL) instead of
      //        being ordered by cast()'s numeric-prefix reading of their text
      // key 4: text value, ASCII-only case fold (COLLATE NOCASE)
      sqlite3_str_appendf(sql_str,
                          " order by case when trim(coalesce(\"%w\",'')) = '' then 1 else 0 end asc"
                          ", case when trim(coalesce(\"%w\",'')) not glob '*[^0-9.eE+-]*'"
                          " and trim(coalesce(\"%w\",'')) glob '*[0-9]*' then 0 else 1 end %s"
                          ", case when trim(coalesce(\"%w\",'')) not glob '*[^0-9.eE+-]*'"
                          " and trim(coalesce(\"%w\",'')) glob '*[0-9]*'"
                          " then cast(trim(coalesce(\"%w\",'')) as numeric) end %s"
                          ", trim(coalesce(\"%w\",'')) collate nocase %s"
                          ", %s asc",
                          key, key, key, dir_sql, key, key, key, dir_sql, key, dir_sql, rid);
  }
  if (sqlite3_str_errcode(sql_str) != SQLITE_OK) {
    zst = sqlite3_str_errcode(sql_str) == SQLITE_NOMEM ? zsvsheet_status_memory : zsvsheet_status_error;
    zsvsheet_ui_buffer_set_status(buff, "Unexpected error preparing SQL");
    goto cleanup;
  }
  zst = zsv_sqlite3_to_csv(ctx, zdb, sqlite3_str_value(sql_str), &err_msg, NULL, NULL, NULL);
  if (zst != zsvsheet_status_ok) {
    if (zst == zsvsheet_status_no_data)
      zsvsheet_ui_buffer_set_status(buff, "No results returned");
    else
      zsvsheet_ui_buffer_set_status(buff, err_msg ? err_msg : "Unexpected error preparing SQL");
    goto cleanup;
  }
  buff = zsvsheet_buffer_current(ctx); // the new (sorted) buffer
  while (!zsvsheet_ui_buffer_index_ready(buff, 0))
    napms(200); // sleep for 200ms, then check index again
  free(status);
  if (asprintf(&status, "Sorted by %s (%s%s)", is_expr ? expr_buffer : key, dir == 'd' ? "descending" : "ascending",
               flags == 'n'   ? ", integer"
               : flags == 'f' ? ", float"
                              : "") == -1)
    status = NULL; // asprintf leaves its output indeterminate on failure
  if (status)
    zsvsheet_ui_buffer_set_status(buff, status);
  goto cleanup;

usage: // an input error, not a failure: zst stays ok
  zsvsheet_ui_buffer_set_status(buff, ctx->proc_id == zsvsheet_builtin_proc_sort_cur_col_desc
                                        ? "usage: sortdesc [<column>] [asc|desc] [n|f]"
                                        : "usage: sort [<column>] [asc|desc] [n|f]");
cleanup:
  zsv_sqlite3_db_delete(zdb);
  if (sql_str)
    sqlite3_free(sqlite3_str_finish(sql_str));
  free(key);
  free(status);
  return zst;
}
