/*
 * Copyright (C) 2021-2022 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 *
 * 2toon: streaming CSV to TOON converter, or SQLite3 DB to TOON converter.
 * Mirrors 2json.c, but emits TOON (https://github.com/toon-format/spec) via
 * toonwriter in place of jsonwriter. The db->TOON serialization below mirrors
 * zsv_dbtable2json() (app/utils/db.c); it is kept local so the shared db.o
 * (used by 2json) need not depend on toonwriter.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <toonwriter.h>
#include <json2toon.h>
#include <sqlite3.h>

#define ZSV_COMMAND 2toon
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/string.h>

struct zsv_2toon_header {
  struct zsv_2toon_header *next;
  char *name;
};

#define LQ_2TOON_MAX_INDEXES 32

struct zsv_2toon_data {
  zsv_parser parser;
  toonwriter_handle toonw;

  size_t rows_processed; // includes header row

  struct {
    const char *clauses[LQ_2TOON_MAX_INDEXES];
    char unique[LQ_2TOON_MAX_INDEXES];
    unsigned count;
  } indexes;

  struct zsv_2toon_header *headers, *current_header;
  struct zsv_2toon_header **headers_next;

  char *db_tablename;

#define ZSV_TOON_SCHEMA_OBJECT 1
#define ZSV_TOON_SCHEMA_DATABASE 2
  unsigned char schema : 2;
  unsigned char no_header : 1;
  unsigned char no_empty : 1;
  unsigned char err : 1;
  unsigned char from_db : 1;
  unsigned char compact : 1;
  unsigned char from_json : 1; // --from-json: input is JSON, convert to TOON
};

static void zsv_2toon_cleanup(struct zsv_2toon_data *data) {
  for (struct zsv_2toon_header *next, *h = data->headers; h; h = next) {
    next = h->next;
    if (h->name)
      free(h->name);
    free(h);
  }
  free(data->db_tablename);
}

static void write_header_cell(struct zsv_2toon_data *data, const unsigned char *utf8_value, size_t len) {
  if (data->schema == ZSV_TOON_SCHEMA_OBJECT) {
    struct zsv_2toon_header *h;
    if (!(h = calloc(1, sizeof(*h)))) {
      fprintf(stderr, "Out of memory!\n");
      data->err = 1;
    } else {
      *data->headers_next = h;
      data->headers_next = &h->next;
      if ((h->name = malloc(len + 1))) {
        memcpy(h->name, utf8_value, len);
        h->name[len] = '\0';
      }
    }
  } else {
    // to do: add options to set data type, etc
    toonwriter_start_object(data->toonw);
    toonwriter_object_key(data->toonw, "name");
    toonwriter_strn(data->toonw, utf8_value, len);
    toonwriter_end(data->toonw);
  }
}

static void write_data_cell(struct zsv_2toon_data *data, const unsigned char *utf8_value, size_t len) {
  if (data->schema == ZSV_TOON_SCHEMA_OBJECT) {
    if (!data->current_header)
      return;
    char *current_header_name = data->current_header->name;
    data->current_header = data->current_header->next;

    if (len || !data->no_empty)
      toonwriter_object_key(data->toonw, current_header_name);
    else
      return;
  }
  toonwriter_strn(data->toonw, utf8_value, len);
}

static char *zsv_2toon_db_first_tname(sqlite3 *db) {
  char *tname = NULL;
  sqlite3_stmt *stmt = NULL;
  const char *sql = "select name from sqlite_master where type = 'table'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    fprintf(stderr, "Unable to prepare %s: %s\n", sql, sqlite3_errmsg(db));
  else if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    if (text) {
      int len = sqlite3_column_bytes(stmt, 0);
      tname = zsv_memdup(text, len);
    }
  }
  if (stmt)
    sqlite3_finalize(stmt);
  return tname;
}

// starts_w_str_underscore(): returns 1 if s starts with prefix
// (case-insensitive), followed by underscore. Mirrors app/utils/db.c.
static char starts_w_str_underscore(const unsigned char *s, size_t s_len, const unsigned char *prefix) {
  char result = 0;
  unsigned char *s_lc = zsv_strtolowercase(s, &s_len);
  size_t pfx_len = strlen((const char *)prefix);
  unsigned char *prefix_lc = zsv_strtolowercase(prefix, &pfx_len);
  if (pfx_len + 1 < s_len && !memcmp(s_lc, prefix_lc, pfx_len) && s_lc[pfx_len] == '_')
    result = 1;
  free(s_lc);
  free(prefix_lc);
  return result;
}

// zsv_dbtable2toon(): convert a db table to TOON. Mirrors zsv_dbtable2json()
// (app/utils/db.c), kept local to avoid a toonwriter dependency in shared db.o.
static int zsv_dbtable2toon(sqlite3 *db, const char *tname, toonwriter_handle toonw, size_t limit) {
  int err = 0;
  const char *index_sql = "select name, sql from sqlite_master where type = 'index' and tbl_name = :tbl_name";
  const char *unique_sql = "select 1 from PRAGMA_index_list(?) where name = ? and [unique] <> 0";
  sqlite3_str *data_sql = sqlite3_str_new(db);
  if (data_sql) {
    sqlite3_str_appendf(data_sql, "select * from \"%w\"", tname);
    sqlite3_stmt *data_stmt = NULL;
    sqlite3_stmt *index_stmt = NULL;
    sqlite3_stmt *unique_stmt = NULL;
    int colcount = 0;

    err = 1;
    if (sqlite3_prepare_v2(db, sqlite3_str_value(data_sql), -1, &data_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", sqlite3_str_value(data_sql), sqlite3_errmsg(db));
    else if (!(colcount = sqlite3_column_count(data_stmt)))
      fprintf(stderr, "No columns found in table %s\n", tname);
    else if (sqlite3_prepare_v2(db, index_sql, -1, &index_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", index_sql, sqlite3_errmsg(db));
    else if (sqlite3_prepare_v2(db, unique_sql, -1, &unique_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", unique_sql, sqlite3_errmsg(db));
    else {
      err = 0;
      toonwriter_start_array(toonw); // output is an array with 2 items: meta and data

      // ----- meta: columns and index info
      toonwriter_start_object(toonw);

      toonwriter_object_cstr(toonw, "name", tname);

      // indexes
      toonwriter_object_object(toonw, "indexes"); // indexes
      sqlite3_bind_text(index_stmt, 1, tname, (int)strlen(tname), SQLITE_STATIC);
      while (sqlite3_step(index_stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(index_stmt, 0);
        const unsigned char *ix_sql = sqlite3_column_text(index_stmt, 1);
        size_t len = text ? sqlite3_column_bytes(index_stmt, 0) : 0;
        size_t ix_sql_len = ix_sql ? sqlite3_column_bytes(index_stmt, 1) : 0;

        if (text && ix_sql && len && ix_sql_len) {
          // on: for now we just look for the first and last parens
          const unsigned char *first_paren = memchr(ix_sql, '(', ix_sql_len);
          const unsigned char *last_paren = ix_sql + ix_sql_len;
          while (first_paren && last_paren > first_paren + 1 && *last_paren != ')')
            last_paren--;
          if (first_paren && last_paren > first_paren) {
            // name

            // strip the leading "tablename_" from the index name
            const char *ix_name = (const char *)text;
            size_t ix_name_len = len;
            if (ix_name_len > strlen(tname) + 1 &&
                starts_w_str_underscore((const unsigned char *)ix_name, ix_name_len, (const unsigned char *)tname)) {
              ix_name += strlen(tname) + 1;
              ix_name_len -= strlen(tname) + 1;
            }
            toonwriter_object_keyn(toonw, (const char *)ix_name, ix_name_len);

            // ix obj
            toonwriter_start_object(toonw);

            // on
            toonwriter_object_strn(toonw, "on", first_paren + 1, last_paren - first_paren - 1);

            // unique
            sqlite3_bind_text(unique_stmt, 1, tname, (int)strlen(tname), SQLITE_STATIC);
            sqlite3_bind_text(unique_stmt, 2, (const char *)text, len, SQLITE_STATIC);
            if (sqlite3_step(unique_stmt) == SQLITE_ROW)
              toonwriter_object_bool(toonw, "unique", 1);
            sqlite3_reset(unique_stmt);

            // end ix obj
            toonwriter_end_object(toonw);
          }
        }
      }
      toonwriter_end_object(toonw); // end indexes

      // columns
      toonwriter_object_array(toonw, "columns");
      for (int i = 0; i < colcount; i++) {
        const char *colname = sqlite3_column_name(data_stmt, i);
        toonwriter_start_object(toonw);
        toonwriter_object_cstr(toonw, "name", colname);
        const char *dtype = sqlite3_column_decltype(data_stmt, i);
        if (dtype)
          toonwriter_object_cstr(toonw, "datatype", dtype);

        // TO DO: collate nocase etc
        toonwriter_end_object(toonw);
      }
      toonwriter_end_array(toonw); // end columns

      toonwriter_end_object(toonw); // end meta obj

      // ------ data: array of rows
      toonwriter_start_array(toonw);
      // for each row
      size_t count = 0;
      while (sqlite3_step(data_stmt) == SQLITE_ROW) {
        toonwriter_start_array(toonw); // start row
        for (int i = 0; i < colcount; i++) {
          const unsigned char *text = sqlite3_column_text(data_stmt, i);
          if (text) {
            int len = sqlite3_column_bytes(data_stmt, i);
            toonwriter_strn(toonw, text, len);
          } else
            toonwriter_null(toonw);
        }
        toonwriter_end_array(toonw); // end row
        if (limit && ++count >= limit)
          break;
      }
      toonwriter_end_array(toonw);

      toonwriter_end_array(toonw); // end of output
    }
    if (data_stmt)
      sqlite3_finalize(data_stmt);
    if (index_stmt)
      sqlite3_finalize(index_stmt);
    if (unique_stmt)
      sqlite3_finalize(unique_stmt);

    sqlite3_free(sqlite3_str_finish(data_sql));
  } // if data_sql
  return err;
}

static void zsv_2toon_row(void *ctx) {
  struct zsv_2toon_data *data = ctx;
  unsigned int cols = zsv_cell_count(data->parser);
  if (cols) {
    char obj = 0;
    char arr = 0;
    if (!data->rows_processed) {          // header row
      toonwriter_start_array(data->toonw); // start array of rows
      if (data->schema == ZSV_TOON_SCHEMA_DATABASE) {
        toonwriter_start_object(data->toonw); // start this row
        obj = 1;

        if (data->db_tablename)
          toonwriter_object_cstr(data->toonw, "name", data->db_tablename);

        // to do: check index syntax (as of now, we just take argument value
        // as-is and assume it will translate into a valid SQLITE3 command)
        char have_index = 0;
        for (unsigned i = 0; i < data->indexes.count; i++) {
          const char *name_start = data->indexes.clauses[i];
          const char *on = strstr(name_start, " on ");
          if (on) {
            on += 4;
            while (*on == ' ')
              on++;
          }
          if (!on || !*on)
            continue;

          const char *name_end = name_start;
          while (name_end && *name_end && *name_end != ' ')
            name_end++;

          if (name_end > name_start) {
            if (!have_index) {
              have_index = 1;
              toonwriter_object_object(data->toonw, "indexes");
            }
            char *tmp = zsv_memdup(name_start, name_end - name_start);
            toonwriter_object_object(data->toonw, tmp); // this index
            free(tmp);
            toonwriter_object_cstr(data->toonw, "on", on);
            if (data->indexes.unique[i])
              toonwriter_object_bool(data->toonw, "unique", 1);
            toonwriter_end_object(data->toonw); // end this index
          }
        }
        if (have_index)
          toonwriter_end_object(data->toonw); // indexes

        toonwriter_object_array(data->toonw, "columns");
        arr = 1;
      } else if (data->schema != ZSV_TOON_SCHEMA_OBJECT) {
        toonwriter_start_array(data->toonw); // start this row
        arr = 1;
      }
    } else { // processing a data row, not header row
      if (data->schema == ZSV_TOON_SCHEMA_OBJECT) {
        toonwriter_start_object(data->toonw); // start this row
        obj = 1;
      } else {
        if (data->rows_processed == 1 && data->schema == ZSV_TOON_SCHEMA_DATABASE)
          toonwriter_start_array(data->toonw); // start the table-data element
        toonwriter_start_array(data->toonw);   // start this row
        arr = 1;
      }
    }

    for (unsigned int i = 0; i < cols; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      // output this cell
      if (!data->rows_processed && !data->no_header) // this is header row
        write_header_cell(data, cell.str, cell.len);
      else
        write_data_cell(data, cell.str, cell.len);
    }

    // end this row
    if (arr)
      toonwriter_end_array(data->toonw);
    if (obj)
      toonwriter_end_object(data->toonw);
    data->rows_processed++;
  }
  data->current_header = data->headers;
}

static int zsv_db2toon(const char *input_filename, char **tname, toonwriter_handle toonw) {
  sqlite3 *db;
  int rc =
    sqlite3_open_v2(input_filename, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_SHAREDCACHE, NULL);
  int err = 0;
  if (!db)
    fprintf(stderr, "Unable to open db at %s\n", input_filename), err = 1;
  else if (rc != SQLITE_OK)
    fprintf(stderr, "Unable to open db at %s: %s\n", input_filename, sqlite3_errmsg(db)), err = 1;
  else if (!*tname)
    *tname = zsv_2toon_db_first_tname(db);

  if (!*tname)
    fprintf(stderr, "No table name provided, and none found in %s\n", input_filename), err = 1;
  else
    err = zsv_dbtable2toon(db, *tname, toonw, 0);
  if (db)
    sqlite3_close(db);
  return err;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  struct zsv_opts opts = *optsp;
  struct zsv_2toon_data data = {0};
  data.headers_next = &data.headers;

  const char *usage[] = {
    ZSV_USAGE_PROG " " APPNAME ": streaming CSV to TOON converter, or SQLite3 DB to TOON converter",
    "",
    "Usage: " ZSV_USAGE_PROG " " APPNAME " [options] [file.csv]",
    "",
    "Options:",
    "  -h,--help                     : show usage",
    "  -o,--output <filename>        : filename to write output to",
    "  --compact                     : output compact TOON",
    "  --from-db                     : input is sqlite3 database",
    "  --from-json                   : input is JSON; convert JSON to TOON",
    "  --db-table <table_name>       : name of table in input database to convert",
    "  --object                      : output as array of objects",
    "  --no-empty                    : omit empty properties (only with --object)",
    "  --database                    : output in database schema",
    "  --no-header                   : treat the header row as a data row",
    "  --index <name_on_expr>        : add index to database schema",
    "  --unique-index <name_on_expr> : add unique index to database schema",
    NULL,
  };

  FILE *out = NULL;
  const char *input_path = NULL;
  enum zsv_status err = zsv_status_ok;
  int done = 0;

  for (int i = 1; !err && !done && i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      zsv_print_usage(usage);
      done = 1;
    } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if (++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i - 1]), err = zsv_status_error;
      else if (out && out != stdout)
        fprintf(stderr, "Output file specified more than once\n"), err = zsv_status_error;
      else if (!(out = fopen(argv[i], "wb")))
        fprintf(stderr, "Unable to open for writing: %s\n", argv[i]), err = zsv_status_error;
    } else if (!strcmp(argv[i], "--index") || !strcmp(argv[i], "--unique-index")) {
      if (++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i - 1]), err = zsv_status_error;
      else if (data.indexes.count > LQ_2TOON_MAX_INDEXES)
        fprintf(stderr, "Max index count exceeded; ignoring %s\n", argv[i]), err = zsv_status_error;
      else if (!strstr(argv[i], " on "))
        fprintf(stderr, "Index value should be in the form of 'index_name on expr'; got %s\n", argv[i]),
          err = zsv_status_error;
      else {
        if (!strcmp(argv[i - 1], "--unique-index"))
          data.indexes.unique[data.indexes.count] = 1;
        data.indexes.clauses[data.indexes.count++] = argv[i];
      }
    } else if (!strcmp(argv[i], "--no-empty")) {
      data.no_empty = 1;
    } else if (!strcmp(argv[i], "--db-table")) {
      if (++i >= argc)
        fprintf(stderr, "%s option requires a table name value\n", argv[i - 1]), err = zsv_status_error;
      else if (data.db_tablename)
        fprintf(stderr, "%s option specified more than once\n", argv[i - 1]), err = zsv_status_error;
      else
        data.db_tablename = strdup(argv[i]);
    } else if (!strcmp(argv[i], "--from-db")) {
      if (++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i - 1]), err = zsv_status_error;
      else if (opts.stream)
        fprintf(stderr, "Input file specified more than once\n"), err = zsv_status_error;
      else if (!(opts.stream = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = zsv_status_error;
      else {
        input_path = argv[i];
        data.from_db = 1;
      }
    } else if (!strcmp(argv[i], "--from-json")) {
      data.from_json = 1;
    } else if (!strcmp(argv[i], "--database") || !strcmp(argv[i], "--object")) {
      if (data.schema)
        fprintf(stderr, "Output schema specified more than once\n"), err = zsv_status_error;
      else if (!strcmp(argv[i], "--database"))
        data.schema = ZSV_TOON_SCHEMA_DATABASE;
      else
        data.schema = ZSV_TOON_SCHEMA_OBJECT;
    } else if (!strcmp(argv[i], "--no-header"))
      data.no_header = 1;
    else if (!strcmp(argv[i], "--compact"))
      data.compact = 1;
    else {
      if (opts.stream)
        fprintf(stderr, "Input file specified more than once\n"), err = zsv_status_error;
      else if (!(opts.stream = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = zsv_status_error;
      else
        input_path = argv[i];
    }
  }

  if (!(err || done)) {
    if (data.from_json && (data.from_db || data.schema || data.no_header || data.no_empty || data.indexes.count))
      fprintf(stderr, "--from-json cannot be combined with --from-db or CSV/schema options\n"), err = zsv_status_error;
    else if (data.indexes.count && data.schema != ZSV_TOON_SCHEMA_DATABASE)
      fprintf(stderr, "--index/--unique-index can only be used with --database\n"), err = zsv_status_error;
    else if (data.no_header && data.schema)
      fprintf(stderr, "--no-header cannot be used together with --object or --database\n"), err = zsv_status_error;
    else if (data.no_empty && data.schema != ZSV_TOON_SCHEMA_OBJECT)
      fprintf(stderr, "--no-empty can only be used with --object\n"), err = zsv_status_error;
    else if (!opts.stream) {
      if (data.from_db)
        fprintf(stderr, "Database input specified, but no input file provided\n"), err = zsv_status_error;
      else {
#ifdef NO_STDIN
        fprintf(stderr, "Please specify an input file\n"), err = zsv_status_error;
#else
        opts.stream = stdin;
#endif
      }
    }
  }

  if (!(err || done)) {
    if (!out)
      out = stdout;
    if (data.from_json) {
      size_t off = 0;
      int rc = json2toon_convert_file(opts.stream, out, NULL, &off);
      if (rc != JSON2TOON_OK)
        fprintf(stderr, "%s: JSON to TOON conversion failed at byte %zu: %s\n", APPNAME, off, json2toon_strerror(rc)),
          err = zsv_status_error;
    } else if (!(data.toonw = toonwriter_new(out, NULL)))
      err = zsv_status_error;
    else {
      if (data.compact)
        toonwriter_set_option(data.toonw, toonwriter_option_compact);
      if (data.from_db) {
        if (opts.stream != stdin) {
          fclose(opts.stream);
          opts.stream = NULL;
        }
        err = zsv_db2toon(input_path, &data.db_tablename, data.toonw);
      } else {
        opts.row_handler = zsv_2toon_row;
        opts.ctx = &data;
        if (zsv_new_with_properties(&opts, custom_prop_handler, input_path, &data.parser) == zsv_status_ok) {
          zsv_handle_ctrl_c_signal();
          while (!data.err && !zsv_signal_interrupted && zsv_parse_more(data.parser) == zsv_status_ok)
            ;
          zsv_finish(data.parser);
          zsv_delete(data.parser);
          toonwriter_end_all(data.toonw);
        }
        err = data.err;
      }
    }
    toonwriter_delete(data.toonw);
  }

  zsv_2toon_cleanup(&data);
  if (opts.stream && opts.stream != stdin)
    fclose(opts.stream);
  if (out && out != stdout)
    fclose(out);
  return err;
}
