/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/string.h>
#include <zsv/utils/arg.h>

#include <unistd.h> // unlink

#ifndef APPNAME
#define APPNAME "sql"
#endif

extern sqlite3_module CsvModule;

#ifndef STRING_LIST
#define STRING_LIST
struct string_list {
  struct string_list *next;
  char *value;
};
#endif

const char *zsv_sql_usage_msg[] =
  {
   APPNAME ": run ad hoc sql on a CSV file",
   "",
#ifdef NO_STDIN
   "Usage: " APPNAME " <filename> [filename ...] <sql>",
#else
   "Usage: " APPNAME " [filename, or - for stdin] [filename ...] <sql>",
#endif
   "  e.g. " APPNAME " myfile.csv 'select * from data'",
   "  e.g. " APPNAME " myfile.csv myfile2.csv 'select * from data inner join data2'",
   "",
   "Loads your CSV file into a table named 'data', then runs your sql, which must start with 'select '.",
   "If multiple files are specified, tables will be named data, data2, data3, ...",
   "",
   "Options:",
   "  -b: output with BOM",
   "  -C, --max-cols <n>: change the maximum allowable columns. must be > 0 and < 2000",
   "  -o <output filename>: name of file to save output to",
   NULL
};

static void zsv_sql_usage() {
  for(int i = 0; zsv_sql_usage_msg[i]; i++)
    fprintf(stderr, "%s\n", zsv_sql_usage_msg[i]);
}

struct zsv_sql_data {
  FILE *in;
  int dummy;
  struct string_list *more_input_filenames;
};

static void zsv_sql_data_init(struct zsv_sql_data *data) {
  memset(data, 0, sizeof(*data));
}

static void zsv_sql_finalize(struct zsv_sql_data *data) {
  (void)data;
}

static void zsv_sql_cleanup(struct zsv_sql_data *data) {
  if(data->in && data->in != stdin)
    fclose(data->in);
  if(data->more_input_filenames) {
    struct string_list *next;
    for(struct string_list *tmp = data->more_input_filenames; tmp; tmp = next) {
      next = tmp->next;
      free(tmp);
    }
  }
  (void)data;
}

static int create_virtual_csv_table(const char *fname, sqlite3 *db,
                                    int max_columns, char **err_msg,
                                    int table_ix) {
  // TO DO: set customizable maximum number of columns to prevent
  // runaway in case no line ends found
  char *sql = NULL;
  char table_name_suffix[64];

  if(table_ix == 0)
    *table_name_suffix = '\0';
  else if(table_ix < 0 || table_ix > 1000)
    return -1;
  else
    snprintf(table_name_suffix, sizeof(table_name_suffix), "%i", table_ix + 1);

  if(max_columns)
    asprintf(&sql, "CREATE VIRTUAL TABLE data%s USING csv(filename='%s',max_columns=%i)", table_name_suffix, fname, max_columns);
           
  else
    asprintf(&sql, "CREATE VIRTUAL TABLE data%s USING csv(filename='%s')", table_name_suffix, fname);

  int rc = sqlite3_exec(db, sql, NULL, NULL, err_msg);
  free(sql);
  return rc;
}
#ifndef MAIN
#define MAIN main
#endif

int MAIN(int argc, const char *argv[]) {
  INIT_CMD_DEFAULT_ARGS();

  if(argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
    zsv_sql_usage();
  else {
    struct zsv_sql_data data;
    zsv_sql_data_init(&data);
    int max_cols = 0;
    const char *input_filename = NULL;
    const char *my_sql = NULL;
    struct string_list **next_input_filename = &data.more_input_filenames;

    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
    int err = 0;
    for(int arg_i = 1; !err && arg_i < argc; arg_i++) {
      const char *arg = argv[arg_i];
      if(!my_sql && strlen(arg) > strlen("select ")
         && !zsv_strincmp(
                          (const unsigned char *)"select ", strlen("select "),
                          (const unsigned char *)arg, strlen("select "))
         )
        my_sql = arg;
      else if(!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
        if(!(++arg_i < argc)) {
          fprintf(stderr, "option %s requires a filename\n", arg);
          err = 1;
        } else if(!(writer_opts.stream = fopen(argv[arg_i], "wb"))) {
          fprintf(stderr, "Could not open for writing: %s\n", argv[arg_i]);
          err = 1;
        }
      } else if(!strcmp(arg, "-b"))
        writer_opts.with_bom = 1;

      else if(!strcmp(arg, "-C") || !strcmp(arg, "--max-cols")) {
        if(arg_i+1 < argc && atoi(argv[arg_i+1]) > 0 && atoi(argv[arg_i+1]) <= 2000)
          max_cols = atoi(argv[++arg_i]);
        else {
          fprintf(stderr, "maximum columns value not provided or not between 0 and 2000\n");
          err = 1;
        }
      } else if(*arg != '-') {
        if(!input_filename) {
          input_filename = arg;
          if(!(data.in = fopen(arg, "rb"))) {
            fprintf(stderr, "Unable to open for reading: %s\n", arg);
            err = 1;
          }
        } else { // another input file
          FILE *tmp_f;
          if(!(tmp_f = fopen(arg, "rb"))) {
            fprintf(stderr, "Unable to open %s for reading\n", arg);
            err = 1;
          } else {
            fclose(tmp_f);
            struct string_list *tmp = calloc(1, sizeof(**next_input_filename));
            if(!tmp)
              fprintf(stderr, "Out of memory!\n");
            else {
              tmp->value = (char *)arg;
              *next_input_filename = tmp;
              next_input_filename = &tmp->next;
            }
          }
        }
      } else {
        err = 1;
        fprintf(stderr, "Unrecognized option: %s\n", arg);
      }
    }

    if(!data.in || !input_filename) {
#ifdef NO_STDIN
      fprintf(stderr, "Please specify an input file\n");
      err = 1;
#else
      data.in = stdin;
#endif
    }

    if(!my_sql) {
      fprintf(stderr, "No sql command specified\n");
      err = 1;
    }

    if(err) {
      zsv_sql_cleanup(&data);
      return 1;
    }

    if(data.in != stdin) {
      fclose(data.in);
      data.in = NULL;
    }

    FILE *f = NULL;
    char *tmpfn = NULL;
    if(input_filename) {
      f = fopen(input_filename, "rb");
      if(!f)
        fprintf(stderr, "Unable to open %s for reading\n", input_filename);
    } else
      f = stdin;

    if(f == stdin) {
      tmpfn = zsv_get_temp_filename("zsv_sql");
      if(!tmpfn) {
        fprintf(stderr, "Unable to create temp file name\n");
      } else {
        FILE *tmpf = fopen(tmpfn, "wb");
        if(!tmpf)
          fprintf(stderr, "Unable to open temp file %s\n", tmpfn);
        else {
          char rbuff[1024];
          size_t bytes_read;
          while((bytes_read = fread(rbuff, 1, sizeof(rbuff), stdin)))
            fwrite(rbuff, 1, bytes_read, tmpf);
          f = tmpf;
        }
      }
    }

    if(f) {
      fclose(f); // to do: don't open in the first place
      f = NULL;

      sqlite3 *db = NULL;
      int rc;

      zsv_csv_writer cw = zsv_writer_new(&writer_opts);
      unsigned char cw_buff[1024];
      zsv_writer_set_temp_buff(cw, cw_buff, sizeof(cw_buff));

      char *err_msg = NULL;
      if((rc = sqlite3_open_v2("file::memory:", &db, SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE, NULL)) == SQLITE_OK
         && db
         && (rc = sqlite3_create_module(db, "csv", &CsvModule, 0) == SQLITE_OK)
         && (rc = create_virtual_csv_table(tmpfn ? tmpfn : input_filename, db, max_cols, &err_msg, 0)) == SQLITE_OK
         ) {
        int i = 1;
        for(struct string_list *sl = data.more_input_filenames; sl; sl = sl->next)
          if(create_virtual_csv_table(sl->value, db, max_cols, &err_msg, i++) != SQLITE_OK)
            rc = SQLITE_ERROR;
      }

      if(rc == SQLITE_OK) {
        sqlite3_stmt *stmt;
        err = sqlite3_prepare_v2(db, my_sql, -1, &stmt, NULL);
        if(err != SQLITE_OK)
          fprintf(stderr, "%s:\n  %s\n (or bad CSV/utf8 input)\n\n", sqlite3_errstr(err), my_sql);
        else {
          int col_count = sqlite3_column_count(stmt);

          // write header row
          for(int i = 0; i < col_count; i++) {
            const char *colname = sqlite3_column_name(stmt, i);
            zsv_writer_cell(cw, !i, (const unsigned char *)colname, colname ? strlen(colname) : 0, 1);
          }

          while(sqlite3_step(stmt) == SQLITE_ROW) {
            for(int i = 0; i < col_count; i++) {
              const unsigned char *text = sqlite3_column_text(stmt, i);
              int len = text ? sqlite3_column_bytes(stmt, i) : 0;
              zsv_writer_cell(cw, !i, text, len, 1);
            }
          }
          sqlite3_finalize(stmt);
        }
      }
      if(err_msg) {
        fprintf(stderr, "Error: %s\n", err_msg);
        sqlite3_free(err_msg);
      } else if(!db)
        fprintf(stderr, "Error (unable to open db, code %i): %s\n", rc, sqlite3_errstr(rc));
      else if(rc)
        fprintf(stderr, "Error (code %i): %s\n", rc, sqlite3_errstr(rc));

      if(db)
        sqlite3_close(db);

      zsv_writer_delete(cw);
    }
    if(f)
      fclose(f);
    zsv_sql_finalize(&data);
    zsv_sql_cleanup(&data);

    if(tmpfn) {
      unlink(tmpfn);
      free(tmpfn);
    }
  }
  return 0;
}
