/*
 * Copyright (C) 2021-2022 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jsonwriter.h>
#include <sqlite3.h>

#define ZSV_COMMAND 2json
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/db.h>

struct zsv_2json_header {
  struct zsv_2json_header *next;
  char *name;
};

#define LQ_2JSON_MAX_INDEXES 32

struct zsv_2json_data {
  zsv_parser parser;
  jsonwriter_handle jsw;

  size_t rows_processed; // includes header row

  struct {
    const char *clauses[LQ_2JSON_MAX_INDEXES];
    char unique[LQ_2JSON_MAX_INDEXES];
    unsigned count;
  } indexes;

  struct zsv_2json_header *headers, *current_header;
  struct zsv_2json_header **headers_next;

  char *db_tablename;

#define ZSV_JSON_SCHEMA_OBJECT 1
#define ZSV_JSON_SCHEMA_DATABASE 2
  unsigned char schema:2;
  unsigned char no_header:1;
  unsigned char no_empty:1;
  unsigned char err:1;
  unsigned char from_db:1;
  unsigned char compact:1;
  unsigned char _:1;
};

static void zsv_2json_cleanup(struct zsv_2json_data *data) {
  for(struct zsv_2json_header *next, *h = data->headers; h; h = next) {
    next = h->next;
    if(h->name)
      free(h->name);
    free(h);
  }
  free(data->db_tablename);
}

static void write_header_cell(struct zsv_2json_data *data, const unsigned char *utf8_value, size_t len) {
  if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
    struct zsv_2json_header *h;
    if(!(h = calloc(1, sizeof(*h)))) {
      fprintf(stderr, "Out of memory!\n");
      data->err = 1;
    } else {
      *data->headers_next = h;
      data->headers_next = &h->next;
      if((h->name = malloc(len + 1))) {
        memcpy(h->name, utf8_value, len);
        h->name[len] = '\0';
      }
    }
  } else {
    // to do: add options to set data type, etc
    jsonwriter_start_object(data->jsw);
    jsonwriter_object_key(data->jsw, "name");
    jsonwriter_strn(data->jsw, utf8_value, len);
    jsonwriter_end(data->jsw);
  }
}

static void write_data_cell(struct zsv_2json_data *data, const unsigned char *utf8_value, size_t len) {
  if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
    if(!data->current_header)
      return;
    char *current_header_name = data->current_header->name;
    data->current_header = data->current_header->next;

    if(len || !data->no_empty)
      jsonwriter_object_key(data->jsw, current_header_name);
    else
      return;
  }
  jsonwriter_strn(data->jsw, utf8_value, len);
}

static char *zsv_2json_db_first_tname(sqlite3 *db) {
  char *tname = NULL;
  sqlite3_stmt *stmt = NULL;
  const char *sql = "select name from sqlite_master where type = 'table'";
  if(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    fprintf(stderr, "Unable to prepare %s: %s\n", sql, sqlite3_errmsg(db));
  else if(sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    if(text) {
      int len = sqlite3_column_bytes(stmt, 0);
      tname = zsv_memdup(text, len);
    }
  }
  if(stmt)
    sqlite3_finalize(stmt);
  return tname;
}

static void zsv_2json_row(void *ctx) {
  struct zsv_2json_data *data = ctx;
  unsigned int cols = zsv_cell_count(data->parser);
  if(cols) {
    char obj = 0;
    char arr = 0;
    if(!data->rows_processed) { // header row
      jsonwriter_start_array(data->jsw); // start array of rows
      if(data->schema == ZSV_JSON_SCHEMA_DATABASE) {
        jsonwriter_start_object(data->jsw); // start this row
        obj = 1;

        if(data->db_tablename)
          jsonwriter_object_cstr(data->jsw, "name", data->db_tablename);

        // to do: check index syntax (as of now, we just take argument value
        // as-is and assume it will translate into a valid SQLITE3 command)
        char have_index = 0;
        for(unsigned i = 0; i < data->indexes.count; i++) {
          const char *name_start = data->indexes.clauses[i];
          const char *on = strstr(name_start, " on ");
          if(on) {
            on += 4;
            while(*on == ' ')
              on++;
          }
          if(!on || !*on)
            continue;

          const char *name_end = name_start;
          while(name_end && *name_end && *name_end != ' ')
            name_end++;

          if(name_end > name_start) {
            if(!have_index) {
              have_index = 1;
              jsonwriter_object_object(data->jsw, "indexes");
            }
            char *tmp = zsv_memdup(name_start, name_end - name_start);
            jsonwriter_object_object(data->jsw, tmp); // this index
            free(tmp);
            jsonwriter_object_cstr(data->jsw, "on", on);
            if(data->indexes.unique[i])
              jsonwriter_object_bool(data->jsw, "unique", 1);
            jsonwriter_end_object(data->jsw); // end this index
          }
        }
        if(have_index)
          jsonwriter_end_object(data->jsw); // indexes

        jsonwriter_object_array(data->jsw, "columns");
        arr = 1;
      } else if(data->schema != ZSV_JSON_SCHEMA_OBJECT) {
        jsonwriter_start_array(data->jsw); // start this row
        arr = 1;
      }
    } else { // processing a data row, not header row
      if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
        jsonwriter_start_object(data->jsw); // start this row
        obj = 1;
      } else {
        if(data->rows_processed == 1 && data->schema == ZSV_JSON_SCHEMA_DATABASE)
          jsonwriter_start_array(data->jsw); // start the table-data element
        jsonwriter_start_array(data->jsw); // start this row
        arr = 1;
      }
    }

    for(unsigned int i = 0; i < cols; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      // output this cell
      if(!data->rows_processed && !data->no_header) // this is header row
        write_header_cell(data, cell.str, cell.len);
      else
        write_data_cell(data, cell.str, cell.len);
    }

    // end this row
    if(arr)
      jsonwriter_end_array(data->jsw);
    if(obj)
      jsonwriter_end_object(data->jsw);
    data->rows_processed++;
  }
  data->current_header = data->headers;
}

static int zsv_db2json(const char *input_filename, char **tname, jsonwriter_handle jsw) {
  sqlite3 *db;
  int rc = sqlite3_open_v2(input_filename, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_SHAREDCACHE, NULL);
  int err = 0;
  if(!db)
    fprintf(stderr, "Unable to open db at %s\n", input_filename), err = 1;
  else if(rc != SQLITE_OK )
    fprintf(stderr, "Unable to open db at %s: %s\n", input_filename,
            sqlite3_errmsg(db)), err = 1;
  else if(!*tname)
    *tname = zsv_2json_db_first_tname(db);

  if(!*tname)
    fprintf(stderr, "No table name provided, and none found in %s\n", input_filename), err = 1;
  else
    err = zsv_dbtable2json(db, *tname, jsw, 0);
  return err;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  struct zsv_2json_data data = { 0 };
  data.headers_next = &data.headers;

  const char *usage[] =
    {
      APPNAME ": streaming CSV to json converter, or sqlite3 db to JSON converter",
      "",
      "Usage: ",
      "   " APPNAME " [input.csv] [options]",
      "   " APPNAME " --from-db <sqlite3_filename> [options]",
      "",
      "Options:",
      "  -h, --help",
      "  -o, --output <filename>       : output to specified filename",
      "  --compact                     : output compact JSON",
      "  --from-db                     : input is sqlite3 database",
      "  --db-table <table_name>       : name of table in input database to convert",
      "  --object                      : output as array of objects",
      "  --no-empty                    : omit empty properties (only with --object)",
      "  --database                    : output in database schema",
      "  --no-header                   : treat the header row as a data row",
      "  --index <name on expr>        : add index to database schema",
      "  --unique-index <name on expr> : add unique index to database schema",
      NULL
    };

  FILE *out = NULL;
  const char *input_path = NULL;
  enum zsv_status err = zsv_status_ok;
  int done = 0;
  
  for(int i = 1; !err && !done && i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      for(int j = 0; usage[j]; j++)
        fprintf(stdout, "%s\n", usage[j]);
      done = 1;
    } else if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = zsv_status_error;
      else if(out && out != stdout)
        fprintf(stderr, "Output file specified more than once\n"), err = zsv_status_error;
      else if(!(out = fopen(argv[i], "wb")))
        fprintf(stderr, "Unable to open for writing: %s\n", argv[i]), err = zsv_status_error;
    } else if(!strcmp(argv[i], "--index") || !strcmp(argv[i], "--unique-index")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = zsv_status_error;
      else if(data.indexes.count > LQ_2JSON_MAX_INDEXES)
        fprintf(stderr, "Max index count exceeded; ignoring %s\n", argv[i]), err = zsv_status_error;
      else if(!strstr(argv[i], " on "))
        fprintf(stderr, "Index value should be in the form of 'index_name on expr'; got %s\n", argv[i]), err = zsv_status_error;
      else {
        if(!strcmp(argv[i-1], "--unique-index"))
          data.indexes.unique[data.indexes.count] = 1;
        data.indexes.clauses[data.indexes.count++] = argv[i];
      }
    } else if(!strcmp(argv[i], "--no-empty")) {
      data.no_empty = 1;
    } else if(!strcmp(argv[i], "--db-table")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a table name value\n", argv[i-1]), err = zsv_status_error;
      else if(data.db_tablename)
        fprintf(stderr, "%s option specified more than once\n", argv[i-1]), err = zsv_status_error;
      else
        data.db_tablename = strdup(argv[i]);
    } else if(!strcmp(argv[i], "--from-db")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = zsv_status_error;
      else if(opts->stream)
        fprintf(stderr, "Input file specified more than once\n"), err = zsv_status_error;
      else if(!(opts->stream = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = zsv_status_error;
      else {
        input_path = argv[i];
        data.from_db = 1;
      }
    } else if(!strcmp(argv[i], "--database") || !strcmp(argv[i], "--object")) {
      if(data.schema)
        fprintf(stderr, "Output schema specified more than once\n"), err = zsv_status_error;
      else if(!strcmp(argv[i], "--database"))
        data.schema = ZSV_JSON_SCHEMA_DATABASE;
      else
        data.schema = ZSV_JSON_SCHEMA_OBJECT;
    } else if(!strcmp(argv[i], "--no-header"))
      data.no_header = 1;
    else if(!strcmp(argv[i], "--compact"))
      data.compact = 1;
    else {
      if(opts->stream)
        fprintf(stderr, "Input file specified more than once\n"), err = zsv_status_error;
      else if(!(opts->stream = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = zsv_status_error;
      else
        input_path = argv[i];
    }
  }

  if(!(err || done)) {
    if(data.indexes.count && data.schema != ZSV_JSON_SCHEMA_DATABASE)
      fprintf(stderr, "--index/--unique-index can only be used with --database\n"), err = zsv_status_error;
    else if(data.no_header && data.schema)
      fprintf(stderr, "--no-header cannot be used together with --object or --database\n"), err = zsv_status_error;
    else if(data.no_empty && data.schema != ZSV_JSON_SCHEMA_OBJECT)
      fprintf(stderr, "--no-empty can only be used with --object\n"), err = zsv_status_error;
    else if(!opts->stream) {
      if(data.from_db)
        fprintf(stderr, "Database input specified, but no input file provided\n"), err = zsv_status_error;
      else {
#ifdef NO_STDIN
        fprintf(stderr, "Please specify an input file\n"), err = zsv_status_error;
#else
        opts->stream = stdin;
#endif
      }
    }
  }

  if(!(err || done)) {
    if(!out)
      out = stdout;
    if(!(data.jsw = jsonwriter_new(out)))
      err = zsv_status_error;
    else {
      if(data.compact)
        jsonwriter_set_option(data.jsw, jsonwriter_option_compact);
      if(data.from_db) {
        if(opts->stream != stdin) {
          fclose(opts->stream);
          opts->stream = NULL;
        }
        err = zsv_db2json(input_path, &data.db_tablename, data.jsw);
      } else {
        opts->row_handler = zsv_2json_row;
        opts->ctx = &data;
        if(zsv_new_with_properties(opts, custom_prop_handler, input_path, opts_used, &data.parser) == zsv_status_ok) {
          zsv_handle_ctrl_c_signal();
          while(!data.err
                && !zsv_signal_interrupted
                && zsv_parse_more(data.parser) == zsv_status_ok)
            ;
          zsv_finish(data.parser);
          zsv_delete(data.parser);
          jsonwriter_end_all(data.jsw);
        }
        err = data.err;
      }
    }
    jsonwriter_delete(data.jsw);
  }

  zsv_2json_cleanup(&data);
  if(opts->stream && opts->stream != stdin)
    fclose(opts->stream);
  if(out && out != stdout)
    fclose(out);
  return err;
}
