/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 *
 * Security regression: unit-test the real zsv_sql_authorizer policy used by
 * `zsv sql` to sandbox uncontrolled user SQL. The policy is a pure function of
 * the sqlite3 action code, so it can be verified directly against the compile-
 * time SQLITE_* constants -- no live database or linking of sqlite3 required.
 *
 * A regression that weakened the allowlist (e.g. allowing SQLITE_INSERT) or the
 * default-deny posture makes this fail.
 */
#include <stdio.h>
#include "sql_authorizer.h"

static int failures = 0;

static void expect(int action, int want, const char *label) {
  int got = zsv_sql_authorizer(NULL, action, NULL, NULL, NULL, NULL);
  if (got != want) {
    fprintf(stderr, "FAIL: action %s -> %d, expected %d\n", label, got, want);
    failures++;
  }
}

int main(void) {
  // allowed: everything a read-only SELECT needs
  expect(SQLITE_SELECT, SQLITE_OK, "SQLITE_SELECT");
  expect(SQLITE_READ, SQLITE_OK, "SQLITE_READ");
  expect(SQLITE_FUNCTION, SQLITE_OK, "SQLITE_FUNCTION");
  expect(SQLITE_RECURSIVE, SQLITE_OK, "SQLITE_RECURSIVE");

  // denied: every write / DDL / attach / side-effecting action
  expect(SQLITE_INSERT, SQLITE_DENY, "SQLITE_INSERT");
  expect(SQLITE_UPDATE, SQLITE_DENY, "SQLITE_UPDATE");
  expect(SQLITE_DELETE, SQLITE_DENY, "SQLITE_DELETE");
  expect(SQLITE_DROP_TABLE, SQLITE_DENY, "SQLITE_DROP_TABLE");
  expect(SQLITE_DROP_INDEX, SQLITE_DENY, "SQLITE_DROP_INDEX");
  expect(SQLITE_CREATE_TABLE, SQLITE_DENY, "SQLITE_CREATE_TABLE");
  expect(SQLITE_CREATE_INDEX, SQLITE_DENY, "SQLITE_CREATE_INDEX");
  expect(SQLITE_CREATE_VTABLE, SQLITE_DENY, "SQLITE_CREATE_VTABLE");
  expect(SQLITE_ALTER_TABLE, SQLITE_DENY, "SQLITE_ALTER_TABLE");
  expect(SQLITE_ATTACH, SQLITE_DENY, "SQLITE_ATTACH");
  expect(SQLITE_DETACH, SQLITE_DENY, "SQLITE_DETACH");
  expect(SQLITE_PRAGMA, SQLITE_DENY, "SQLITE_PRAGMA");
  expect(SQLITE_TRANSACTION, SQLITE_DENY, "SQLITE_TRANSACTION");

  if (failures) {
    fprintf(stderr, "%d authorizer policy check(s) failed\n", failures);
    return 1;
  }
  printf("authorizer policy: all checks passed\n");
  return 0;
}
