/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_SQL_AUTHORIZER_H
#define ZSV_SQL_AUTHORIZER_H

#include "external/sqlite3/sqlite3.h"

// zsv_sql_authorizer: restrict user-supplied SQL to read-only operations.
// zsv sql's contract is to run a caller-provided SELECT over the input CSV(s).
// Default-deny (allowing only the actions a read-only SELECT needs) defensively
// bounds what an untrusted/uncontrolled SQL string can do: it cannot mutate data,
// ATTACH other database files, load extensions, or create/drop schema objects.
// Install (via sqlite3_set_authorizer) only after the input tables are
// registered, so table setup is unaffected. Resolves CodeQL cpp/sql-injection
// for the prepare of user SQL. Defined here (rather than in sql.c) so the policy
// can be unit-tested directly against the sqlite3 action codes.
static inline int zsv_sql_authorizer(void *ctx, int action, const char *a3, const char *a4, const char *a5,
                                     const char *a6) {
  (void)ctx;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  switch (action) {
  case SQLITE_SELECT:    // a SELECT statement (issued once per select)
  case SQLITE_READ:      // read a column value
  case SQLITE_FUNCTION:  // call a SQL function (this build registers only safe built-ins)
  case SQLITE_RECURSIVE: // recursive CTE
    return SQLITE_OK;
  default:
    return SQLITE_DENY;
  }
}

#endif
