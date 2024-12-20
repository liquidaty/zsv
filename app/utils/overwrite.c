#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <sqlite3.h>

#include <zsv.h>
#include <zsv/utils/overwrite.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/file.h>
#include <zsv/utils/string.h>

#define zsv_overwrite_sqlite3_prefix "sqlite3://"
#define zsv_overwrite_sql_prefix "sql="

/* Overwrite structure for CSV or SQLITE3 sources */
void *zsv_overwrite_context_new(struct zsv_overwrite_opts *opts) {
  struct zsv_overwrite_ctx *ctx = calloc(1, sizeof(*ctx));
  if (ctx && opts->src) {
    if (!(ctx->src = strdup(opts->src)))
      fprintf(stderr, "zsv_overwrite_context_new: out of memory!\n");
  }
  return ctx;
}

enum zsv_status zsv_overwrite_context_delete(void *h) {
  struct zsv_overwrite_ctx *ctx = h;
  if (ctx->sqlite3.filename)
    free(ctx->sqlite3.filename);
  if (ctx->sqlite3.stmt)
    sqlite3_finalize(ctx->sqlite3.stmt);
  if (ctx->sqlite3.db)
    sqlite3_close(ctx->sqlite3.db);
  if (ctx->csv.f)
    fclose(ctx->csv.f);
  if (ctx->csv.parser)
    zsv_delete(ctx->csv.parser);
  free(ctx->src);
  free(ctx);
  return zsv_status_ok;
}

static enum zsv_status zsv_next_overwrite_csv(void *h, struct zsv_overwrite_data *odata) {
  struct zsv_overwrite_ctx *ctx = h;
  if (zsv_next_row(ctx->csv.parser) != zsv_status_row)
    odata->have = 0;
  else {
    // row, column, value
    struct zsv_cell row = zsv_get_cell(ctx->csv.parser, 0);
    struct zsv_cell col = zsv_get_cell(ctx->csv.parser, 1);
    struct zsv_cell val = zsv_get_cell(ctx->csv.parser, 2);
    struct zsv_cell author = {0};
    struct zsv_cell timestamp = {0};
    struct zsv_cell old_value = {0};
    if (ctx->author_ix)
      author = zsv_get_cell(ctx->csv.parser, ctx->author_ix);
    if (ctx->timestamp_ix)
      timestamp = zsv_get_cell(ctx->csv.parser, ctx->timestamp_ix);
    if (ctx->old_value_ix)
      old_value = zsv_get_cell(ctx->csv.parser, ctx->old_value_ix);
    if (row.len && col.len) {
      char *end = (char *)(row.str + row.len);
      char **endp = &end;
      odata->row_ix = strtoumax((char *)row.str, endp, 10);
      end = (char *)(col.str + col.len);
      odata->col_ix = strtoumax((char *)col.str, endp, 10);
      odata->val = val;
      odata->author = author;
      odata->old_value = old_value;
    } else {
      odata->row_ix = 0;
      odata->col_ix = 0;
      odata->val.len = 0;
      odata->old_value.len = 0;
      odata->author.len = 0;
    }

    if (timestamp.len) {
      char *end = (char *)(timestamp.str + timestamp.len);
      char **endp = &end;
      odata->timestamp = strtoumax((char *)timestamp.str, endp, 10);
    }
  }
  return zsv_status_ok;
}

static enum zsv_status zsv_next_overwrite_sqlite3(void *h, struct zsv_overwrite_data *odata) {
  struct zsv_overwrite_ctx *ctx = h;
  if (odata->have) {
    sqlite3_stmt *stmt = ctx->sqlite3.stmt;
    if (stmt) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        // row, column, value
        odata->row_ix = sqlite3_column_int64(stmt, 0);
        odata->col_ix = sqlite3_column_int64(stmt, 1);
        odata->val.str = (unsigned char *)sqlite3_column_text(stmt, 2);
        odata->val.len = sqlite3_column_bytes(stmt, 2);
        odata->timestamp = sqlite3_column_int64(stmt, 3);
        odata->author.str = (unsigned char *)sqlite3_column_text(stmt, 4);
        odata->author.len = sqlite3_column_bytes(stmt, 4);
      } else {
        odata->row_ix = 0;
        odata->col_ix = 0;
        odata->val.len = 0;
        odata->timestamp = 0;
        odata->author.len = 0;
        odata->have = 0;
      }
    }
  }
  return zsv_status_ok;
}

enum zsv_status zsv_overwrite_next(void *h, struct zsv_overwrite_data *odata) {
  struct zsv_overwrite_ctx *ctx = h;
  return ctx->next(ctx, odata);
}

static const char *get_safe_sql_query(sqlite3 *db, const char *user_sql) {
  static const char *default_query =
    "select row, column, value, timestamp, author from overwrites order by row, column";

  // Handle NULL or empty input
  if (!user_sql || !*user_sql)
    return default_query;

  if (!db)
    return default_query;

  sqlite3_stmt *stmt = NULL;
  const char *tail = NULL;

  // Try to prepare the statement (parse SQL)
  int rc = sqlite3_prepare_v2(db, user_sql, -1, &stmt, &tail);

  // Check if parsing succeeded and we got a valid statement
  if (rc != SQLITE_OK || !stmt) {
    if (stmt)
      sqlite3_finalize(stmt);
    return default_query;
  }

  // Verify it's a single statement (no additional statements in tail)
  if (tail && *tail != '\0') {
    sqlite3_finalize(stmt);
    return default_query;
  }

  // Verify it's a read-only (SELECT) statement
  if (!sqlite3_stmt_readonly(stmt)) {
    sqlite3_finalize(stmt);
    return default_query;
  }

  // Verify required columns are present
  int col_count = sqlite3_column_count(stmt);
  int has_row = 0, has_column = 0, has_value = 0;

  for (int i = 0; i < col_count; i++) {
    const char *col_name = sqlite3_column_name(stmt, i);
    if (!col_name)
      continue;

    if (strcmp(col_name, "row") == 0)
      has_row = 1;
    else if (strcmp(col_name, "column") == 0)
      has_column = 1;
    else if (strcmp(col_name, "value") == 0)
      has_value = 1;
  }

  sqlite3_finalize(stmt);

  // Ensure required columns are present (timestamp is optional)
  if (!has_row || !has_column || !has_value)
    return default_query;

  return user_sql;
}

static enum zsv_status zsv_overwrite_init_sqlite3(struct zsv_overwrite_ctx *ctx, const char *source, size_t len) {
  char ok = 0;
  size_t pfx_len;
  const char *user_sql = NULL;

  if (len > (pfx_len = strlen(zsv_overwrite_sqlite3_prefix)) &&
      !memcmp(source, zsv_overwrite_sqlite3_prefix, pfx_len)) {
    ctx->sqlite3.filename = malloc(len - pfx_len + 1);
    memcpy(ctx->sqlite3.filename, source + pfx_len, len - pfx_len);
    ctx->sqlite3.filename[len - pfx_len] = '\0';
    char *q = memchr(ctx->sqlite3.filename, '?', len - pfx_len);
    if (q) {
      *q = '\0';
      q++;
      const char *sql = strstr(q, zsv_overwrite_sql_prefix);
      if (sql)
        user_sql = sql + strlen(zsv_overwrite_sql_prefix);
    }

    if (!ctx->sqlite3.filename || !*ctx->sqlite3.filename) {
      fprintf(stderr, "Missing sqlite3 file name\n");
      return zsv_status_error;
    }

    if (!user_sql || !*user_sql) {
      // to do: detect it from the db
      fprintf(stderr, "Missing sql select statement for sqlite3 overwrite data e.g.:\n"
                      "  select row, column, value from overwrites order by row, column\n");
      return zsv_status_error;
    }
    ok = 1;
  } else if (len > strlen(".sqlite3") && !strcmp(source + len - strlen(".sqlite3"), ".sqlite3")) {
    ctx->sqlite3.filename = strdup(source);
    user_sql = "select * from overwrites order by row, column";
    ok = 1;
  }

  if (ok) {
    int rc = sqlite3_open_v2(ctx->sqlite3.filename, &ctx->sqlite3.db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK || !ctx->sqlite3.db) {
      fprintf(stderr, "%s: %s\n", sqlite3_errstr(rc), ctx->sqlite3.filename);
      return zsv_status_error;
    }

    // Now that we have a valid db connection, validate the SQL
    ctx->sqlite3.sql = get_safe_sql_query(ctx->sqlite3.db, user_sql);
    if (!ctx->sqlite3.sql) {
      fprintf(stderr, "Invalid or unsafe SQL query\n");
      return zsv_status_error;
    }

    rc = sqlite3_prepare_v2(ctx->sqlite3.db, ctx->sqlite3.sql, -1, &ctx->sqlite3.stmt, NULL);
    if (rc != SQLITE_OK || !ctx->sqlite3.stmt) {
      fprintf(stderr, "%s\n", sqlite3_errmsg(ctx->sqlite3.db));
      return zsv_status_error;
    }

    // successful sqlite3 connection
    return zsv_status_ok;
  }

  fprintf(stderr, "Invalid overwrite source: %s\n", source);
  return zsv_status_error;
}

enum zsv_status zsv_overwrite_open(void *h) {
  struct zsv_overwrite_ctx *ctx = h;
  if (!ctx->src)
    return zsv_status_ok;
  char ok = 0;
  size_t src_len = strlen(ctx->src);
  if ((src_len > strlen(zsv_overwrite_sqlite3_prefix) &&
       !memcmp(zsv_overwrite_sqlite3_prefix, ctx->src, strlen(zsv_overwrite_sqlite3_prefix))) ||
      (src_len > strlen(".sqlite3") && !strcmp(ctx->src + src_len - strlen(".sqlite3"), ".sqlite3"))) {
    if (zsv_overwrite_init_sqlite3(ctx, ctx->src, src_len) == zsv_status_ok) {
      ctx->next = zsv_next_overwrite_sqlite3;
      ok = 1;
    }
  } else { // csv
    struct zsv_opts opts = {0};
    ctx->csv.f = opts.stream = fopen(ctx->src, "rb");
    if (!ctx->csv.f) {
      perror(ctx->src);
      return zsv_status_error;
    }
    if (!(ctx->csv.parser = zsv_new(&opts)))
      return zsv_status_memory;

    if (zsv_next_row(ctx->csv.parser) != zsv_status_row) {
      fprintf(stderr, "Unable to fetch any data from overwrite source %s\n", ctx->src);
    } else {
      // row, column, value
      struct zsv_cell row = zsv_get_cell(ctx->csv.parser, 0);
      struct zsv_cell col = zsv_get_cell(ctx->csv.parser, 1);
      struct zsv_cell val = zsv_get_cell(ctx->csv.parser, 2);
      if (row.len < 3 || memcmp(row.str, "row", 3) || col.len < 3 || memcmp(col.str, "col", 3) || val.len < 3 ||
          memcmp(val.str, "val", 3))
        fprintf(stderr, "Warning! overwrite expects 'row,col,value' header, got '%.*s,%.*s,%.*s'\n", (int)row.len,
                row.str, (int)col.len, col.str, (int)val.len, val.str);
      struct zsv_cell next;
      for (size_t i = 3; (next = zsv_get_cell(ctx->csv.parser, i)).len > 0; i++) {
        if (!memcmp(next.str, "timestamp", 9)) {
          ctx->timestamp_ix = i;
        } else if (!memcmp(next.str, "author", 6)) {
          ctx->author_ix = i;
        } else if (!memcmp(next.str, "old value", 9)) {
          ctx->old_value_ix = i;
        }
      }
    }
    ctx->next = zsv_next_overwrite_csv;
    ok = 1;
  }
  return ok ? zsv_status_ok : zsv_status_error;
}

/*
 * zsv_overwrite_auto() returns:
 * - zsv_status_done if a valid overwrite file was found
 * - zsv_status_no_more_input if no overwrite file was found
 * - a different status code on error
 */
enum zsv_status zsv_overwrite_auto(struct zsv_opts *opts, const char *csv_path) {
  enum zsv_status status = zsv_status_error;
  if (opts->overwrite.ctx || opts->overwrite.open || opts->overwrite.next || opts->overwrite.close)
    status = zsv_status_error;
  else {
    unsigned char *overwrite_fn = zsv_cache_filepath((const unsigned char *)csv_path, zsv_cache_type_overwrite, 0, 0);
    if (!overwrite_fn)
      status = zsv_status_memory;
    else if (!zsv_file_exists((char *)overwrite_fn))
      status = zsv_status_no_more_input;
    else {
      struct zsv_overwrite_opts overwrite_opts = {0};
      overwrite_opts.src = (char *)overwrite_fn;
      if (!(opts->overwrite.ctx = zsv_overwrite_context_new(&overwrite_opts)))
        status = zsv_status_memory;
      else {
        opts->overwrite.open = zsv_overwrite_open;
        opts->overwrite.next = zsv_overwrite_next;
        opts->overwrite.close = zsv_overwrite_context_delete;
        status = zsv_status_done;
      }
    }
    free(overwrite_fn);
  }
  return status;
}
