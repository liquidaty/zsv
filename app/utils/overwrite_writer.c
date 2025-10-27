#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/dirs.h>
#include <zsv/utils/file.h>
#include <zsv/utils/os.h>
#include <zsv/utils/overwrite.h>
#include <zsv/utils/overwrite_writer.h>

static enum zsv_status zsv_overwrite_writer_init(struct zsv_overwrite *data) {
  enum zsv_status err = zsv_status_ok;
  int tmp_err = 0;
  if (zsv_mkdirs(data->ctx->src, 1) && !zsv_file_readable(data->ctx->src, &tmp_err, NULL)) {
    err = zsv_status_error;
    perror(data->ctx->src);
    return err;
  }

  sqlite3_stmt *query = NULL;

  if (sqlite3_open_v2(data->ctx->src, &data->ctx->sqlite3.db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ||
      data->mode == zsvsheet_mode_add || data->mode == zsvsheet_mode_bulk || data->mode == zsvsheet_mode_remove ||
      data->mode == zsvsheet_mode_clear) {
    sqlite3_close(data->ctx->sqlite3.db);
    if (sqlite3_open_v2(data->ctx->src, &data->ctx->sqlite3.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) !=
        SQLITE_OK) {
      err = zsv_status_error;
      fprintf(stderr, "Failed to open conn: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
      return err;
    }

    if (sqlite3_exec(data->ctx->sqlite3.db, "PRAGMA foreign_keys = on", NULL, NULL, NULL) != SQLITE_OK) {
      err = zsv_status_error;
      fprintf(stderr, "Could not enable foreign keys: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
      return err;
    }

    if (sqlite3_prepare_v2(data->ctx->sqlite3.db,
                           "CREATE TABLE IF NOT EXISTS overwrites ( row integer, column integer, value string, "
                           "timestamp varchar(25), author varchar(25) );",
                           -1, &query, NULL) == SQLITE_OK) {
      if (sqlite3_step(query) != SQLITE_DONE) {
        err = zsv_status_error;
        fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
        return err;
      }
    } else {
      err = zsv_status_error;
      fprintf(stderr, "Failed to prepare1: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    }

    if (query)
      sqlite3_finalize(query);

    if (sqlite3_prepare_v2(data->ctx->sqlite3.db, "CREATE UNIQUE INDEX overwrites_uix ON overwrites (row, column)", -1,
                           &query, NULL) == SQLITE_OK) {
      if (sqlite3_step(query) != SQLITE_DONE) {
        err = zsv_status_error;
        fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
        return err;
      }
      if (query)
        sqlite3_finalize(query);
    }
  }

  if (!data->ctx->sqlite3.db)
    err = zsv_status_error;

  return err;
}

struct zsv_overwrite *zsv_overwrite_writer_new(struct zsv_overwrite_args *args, struct zsv_overwrite_opts *ctx_opts) {
  struct zsv_overwrite *data = calloc(1, sizeof(*data));
  data->overwrite = calloc(1, sizeof(*data->overwrite));
  data->overwrite->old_value = args->overwrite->old_value;
  data->force = args->force;
  data->all = args->all;
  data->a1 = args->a1;
  data->bulk_file = args->bulk_file;
  data->mode = args->mode;
  data->ctx = zsv_overwrite_context_new(ctx_opts);
  data->overwrite = args->overwrite;
  data->next = args->next;
  struct zsv_csv_writer_options writer_opts = {0};
  data->writer = zsv_writer_new(&writer_opts);

  enum zsv_status err = zsv_status_ok;
  if (data->mode == zsvsheet_mode_list) {
    if ((err = (zsv_overwrite_open(data->ctx)))) // use open when it's read-only
      fprintf(stderr, "Failed to initalize database\n");
  } else {
    if ((err = zsv_overwrite_writer_init(data))) // use init when writing to db
      fprintf(stderr, "Failed to initalize database\n");
  }

  return err == zsv_status_ok ? data : NULL;
}

void zsv_overwrite_writer_delete(struct zsv_overwrite *data) {
  if (data->writer)
    zsv_writer_delete(data->writer);
  if (data->ctx)
    zsv_overwrite_context_delete(data->ctx);

  if (data->overwrite && data->mode != zsvsheet_mode_bulk)
    free(data->overwrite->val.str);

  if (data->all)
    zsv_remove(data->ctx->src);
  free(data);
}

enum zsv_status zsv_overwrite_writer_add(struct zsv_overwrite *data) {
  if (!data->overwrite->val.str)
    return zsv_status_error;
  if (data->force)
    data->ctx->sqlite3.sql =
      "INSERT OR REPLACE INTO overwrites (row, column, value, timestamp, author) VALUES (?, ?, ?, ?, ?)";
  else if (data->overwrite->old_value.len > 0)
    data->ctx->sqlite3.sql = "INSERT OR REPLACE INTO overwrites (row, column, value, timestamp, author) SELECT ?, ?, "
                             "?, ?, ? WHERE EXISTS (SELECT 1 FROM overwrites WHERE value = ?)";
  else
    data->ctx->sqlite3.sql = "INSERT INTO overwrites (row, column, value, timestamp, author) VALUES (?, ?, ?, ?, ?)";

  enum zsv_status err = zsv_status_ok;
  sqlite3_stmt *query = NULL;

  if (sqlite3_prepare_v2(data->ctx->sqlite3.db, data->ctx->sqlite3.sql, -1, &query, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(query, 1, data->overwrite->row_ix);
    sqlite3_bind_int64(query, 2, data->overwrite->col_ix);
    sqlite3_bind_text(query, 3, (const char *)data->overwrite->val.str, data->overwrite->val.len, SQLITE_STATIC);
    if (data->overwrite->timestamp)
      sqlite3_bind_int64(query, 4, data->overwrite->timestamp);
    else
      sqlite3_bind_null(query, 4);
    if (data->overwrite->author.len > 0)
      sqlite3_bind_text(query, 5, (const char *)data->overwrite->author.str, data->overwrite->author.len,
                        SQLITE_STATIC);
    else
      sqlite3_bind_text(query, 5, "", -1, SQLITE_STATIC);

    if (data->overwrite->old_value.len > 0)
      sqlite3_bind_text(query, 6, (const char *)data->overwrite->old_value.str, data->overwrite->old_value.len,
                        SQLITE_STATIC);

    if (sqlite3_step(query) != SQLITE_DONE) {
      err = zsv_status_error;
      if (data->mode == zsvsheet_mode_bulk)
        sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
      fprintf(stderr, "Value already exists at row %zu and column %zu, use --force to force insert\n",
              data->overwrite->row_ix, data->overwrite->col_ix);
    }
  } else {
    err = zsv_status_error;
    if (data->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Failed to prepare2: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  }

  if (query)
    sqlite3_finalize(query);

  return err;
}

enum zsv_status zsv_overwrite_writer_remove(struct zsv_overwrite *data) {
  enum zsv_status err = zsv_status_ok;
  if (data->all) {
    err = zsv_overwrite_writer_clear(data);
    return err;
  }

  data->ctx->sqlite3.sql = data->overwrite->old_value.len > 0
                             ? "DELETE FROM overwrites WHERE row = ? AND column = ? AND value = ?"
                             : "DELETE FROM overwrites WHERE row = ? AND column = ?";

  sqlite3_stmt *query = NULL;
  if (sqlite3_prepare_v2(data->ctx->sqlite3.db, data->ctx->sqlite3.sql, -1, &query, NULL) != SQLITE_OK) {
    err = zsv_status_error;
    if (data->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Could not prepare: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    return err;
  }
  sqlite3_bind_int64(query, 1, data->overwrite->row_ix);
  sqlite3_bind_int64(query, 2, data->overwrite->col_ix);
  if (data->overwrite->old_value.len > 0)
    sqlite3_bind_text(query, 3, (const char *)data->overwrite->old_value.str, data->overwrite->old_value.len,
                      SQLITE_STATIC);

  if (sqlite3_step(query) != SQLITE_DONE) {
    err = zsv_status_error;
    if (data->mode == zsvsheet_mode_bulk)
      sqlite3_exec(data->ctx->sqlite3.db, "ROLLBACK", NULL, NULL, NULL);
    fprintf(stderr, "Could not step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
    return err;
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}

enum zsv_status zsv_overwrite_writer_bulk(struct zsv_overwrite *data) {
  free(data->ctx->src);
  data->ctx->src = (char *)data->bulk_file;
  if (zsv_overwrite_open(data->ctx) != zsv_status_ok) {
    fprintf(stderr, "Could not open\n");
    return zsv_status_error;
  }
  data->overwrite->have = 1;
  data->ctx->row_ix = 1;
  if (sqlite3_exec(data->ctx->sqlite3.db, "BEGIN TRANSACTION", NULL, NULL, NULL) == SQLITE_OK) {
    while (data->ctx->next(data->ctx, data->overwrite) == zsv_status_ok && data->overwrite->have) {
      data->next(data);
      data->overwrite->timestamp = 0;
    }
    if (sqlite3_exec(data->ctx->sqlite3.db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
      fprintf(stderr, "Could not commit changes: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  } else
    fprintf(stderr, "Could not begin transaction: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  return zsv_status_ok;
}

enum zsv_status zsv_overwrite_writer_clear(struct zsv_overwrite *data) {
  enum zsv_status err = zsv_status_ok;
  sqlite3_stmt *query = NULL;
  if (sqlite3_prepare_v2(data->ctx->sqlite3.db, "DELETE FROM overwrites", -1, &query, NULL) == SQLITE_OK) {
    if (sqlite3_step(query) != SQLITE_DONE) {
      err = zsv_status_error;
      fprintf(stderr, "Failed to step: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
      return err;
    }
  } else {
    err = zsv_status_error;
    fprintf(stderr, "Could not prepare: %s\n", sqlite3_errmsg(data->ctx->sqlite3.db));
  }
  if (query)
    sqlite3_finalize(query);
  return err;
}
