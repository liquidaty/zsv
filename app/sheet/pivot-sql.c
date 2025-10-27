static int is_constant_expression(sqlite3* db, const char* expr, int *err) {
  sqlite3_stmt* stmt = NULL;
  // We try to prepare "SELECT [expr]". If this succeeds, the expression
  // does not depend on any columns and is therefore constant.
  char* sql_const_test = sqlite3_mprintf("SELECT %s", expr);
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

enum check_expression_result {
  zsv_pivot_sql_expression_valid = 0,
  zsv_pivot_sql_expression_invalid,
  zsv_pivot_sql_expression_multiple_statements,
  zsv_pivot_sql_expression_multiple_expressions,
  zsv_pivot_sql_expression_other
};

const char *check_expression_result_str(enum check_expression_result rc) {
  switch(rc) {
  case zsv_pivot_sql_expression_valid: return NULL;
  case zsv_pivot_sql_expression_invalid: return "Invalid SQL";
  case zsv_pivot_sql_expression_multiple_statements: return "Please enter only a single expression";
  case zsv_pivot_sql_expression_multiple_expressions: return "Please enter only a single expression";
  case zsv_pivot_sql_expression_other: return "Unknown error";
  }
  return NULL;
}

static int is_str_empty(const char* s) {
  if (!s) return 1;
  while (*s) {
    if (!isspace((unsigned char)*s)) {
      return 0;
    }
    s++;
  }
  return 1;
}

static enum check_expression_result check_expression(sqlite3* db, const char* expr, int *err) {
  sqlite3_stmt* stmt = NULL;
    
  // Prepare "SELECT [expr] FROM data" to see if it's valid in the
  // context of the 'data' table.
  char* sql_valid_test = sqlite3_mprintf("SELECT %s FROM data LIMIT 0", expr);
  if (!sql_valid_test) {
    *err = errno;
    return zsv_pivot_sql_expression_other;
  }

  const char* pzTail = NULL;
  int rc = sqlite3_prepare_v2(db, sql_valid_test, -1, &stmt, &pzTail);
  sqlite3_free(sql_valid_test); // Free the string immediately
  if (rc != SQLITE_OK) {
    if (stmt) sqlite3_finalize(stmt);
    return zsv_pivot_sql_expression_invalid;
  }

  // if we are here, the expression is valid. check it is a single statement
  // pzTail points to the start of the *next* statement. If it's not
  // empty or just whitespace, the user tried to inject a second command.
  if (!is_str_empty(pzTail)) {
    sqlite3_finalize(stmt);
    return zsv_pivot_sql_expression_multiple_statements;
  }

  // Check that we have a single expression (e.g., not "myvalue, 123")
  int col_count = sqlite3_column_count(stmt);
  sqlite3_finalize(stmt);
  if (col_count != 1)
    return zsv_pivot_sql_expression_multiple_expressions;

  return zsv_pivot_sql_expression_valid;
}
