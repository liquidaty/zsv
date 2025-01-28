/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
// #include <sqlite3.h>
#include "external/sqlite3/sqlite3.h"
#include "external/sqlite3/sqlite3_csv_vtab-mem.h"

#define ZSV_COMMAND sql
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/string.h>
#include <zsv/utils/sql.h>
#include "sql_internal.h"

#include <unistd.h> // unlink

#ifndef STRING_LIST
#define STRING_LIST
struct string_list {
  struct string_list *next;
  char *value;
};
#endif

const char *zsv_sql_usage_msg[] = {
  APPNAME ": run ad hoc sql on a CSV file",
  "          or join multiple CSV files on one or more common column(s)",
  "",
#ifdef NO_STDIN
  "Usage: " APPNAME " <filename> [filename ...] <sql | @file.sql>",
#else
  "Usage: " APPNAME " [filename, or - for stdin] [filename ...] <sql | @file.sql | --join-indexes <N,...>>",
#endif
  "  e.g. " APPNAME " file.csv \"select * from data\"",
  "  e.g. " APPNAME " file1.csv file2.csv \"select * from data inner join data2\"",
  "  e.g. " APPNAME " file1.csv file2.csv --join-indexes 1,2",
  "",
  "Loads your CSV file into a table named 'data', then runs your sql, which must start with 'select '.",
  "If multiple files are specified, tables will be named data, data2, data3, ...",
  "",
  "Options:",
  "  --join-indexes <n1...>: specify one or more column names to join multiple files by",
  "                          each n is treated as an index in the first input file that determines a column",
  "                          of the join. For example, if joining two files that, respectively, have columns",
  "                          A,B,C,D and X,B,C,A,Y then `--join-indexes 1,3` will join on columns A and C.",
  "                          When using this option, do not include an sql statement",
  "  -b                    : output with BOM",
  "  -C,--max-cols <n>     : change the maximum allowable columns. must be > 0 and < 2000",
  "  -o <filename>         : filename to save output to",
  "  --memory              : use in-memory instead of temporary db (see https://www.sqlite.org/inmemorydb.html)",
  NULL,
};

static int zsv_sql_usage(FILE *f) {
  for (size_t i = 0; zsv_sql_usage_msg[i]; i++)
    fprintf(f, "%s\n", zsv_sql_usage_msg[i]);
  return f == stderr ? 1 : 0;
}

struct zsv_sql_data {
  FILE *in;
  const char *input_filename;
  struct string_list *more_input_filenames;
  char *sql_dynamic;  // will hold contents of sql file, if any
  char *join_indexes; // will hold contents of join_indexes arg, prefixed and suffixed with a comma
  struct string_list *join_column_names;
  unsigned char in_memory : 1;
  unsigned char _ : 7;
};

static void zsv_sql_finalize(struct zsv_sql_data *data) {
  (void)data;
}

static void zsv_sql_cleanup(struct zsv_sql_data *data) {
  if (data->in && data->in != stdin)
    fclose(data->in);
  free(data->sql_dynamic);
  free(data->join_indexes);
  if (data->join_column_names) {
    struct string_list *next;
    for (struct string_list *tmp = data->join_column_names; tmp; tmp = next) {
      next = tmp->next;
      free(tmp->value);
      free(tmp);
    }
  }

  if (data->more_input_filenames) {
    struct string_list *next;
    for (struct string_list *tmp = data->more_input_filenames; tmp; tmp = next) {
      next = tmp->next;
      free(tmp);
    }
  }
  (void)data;
}

static char is_select_sql(const char *s) {
  return strlen(s) > strlen("select ") && !zsv_strincmp((const unsigned char *)"select ", strlen("select "),
                                                        (const unsigned char *)s, strlen("select "));
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler) {
  /**
   * We need to pass the following data to the sqlite3 virtual table code:
   * a. zsv parser options indicated in the cmd line
   * b. input file path
   * c. cmd-line options used in (a), so that we can print warnings in case of
   *    conflict between (a) and properties of (b)
   */
  int err = 0;
  if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
    err = zsv_sql_usage(argc < 2 ? stderr : stdout);
  else {
    struct zsv_sql_data data = {0};
    const char *my_sql = NULL;
    struct string_list **next_input_filename = &data.more_input_filenames;

    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
    for (int arg_i = 1; !err && arg_i < argc; arg_i++) {
      const char *arg = argv[arg_i];
      if (!strcmp(arg, "--join-indexes")) {
        if (data.join_indexes) {
          fprintf(stderr, "%s specified more than once\n", arg);
          err = 1;
        } else if (!(++arg_i < argc)) {
          fprintf(stderr, "%s option requires a value\n", arg);
          err = 1;
        } else {
          arg = argv[arg_i];
          int have = 0;
          for (const char *s = arg; *s; s++) {
            if (*s == ',')
              ;
            else if ((*s >= '0' && *s <= '9'))
              have = 1;
            else
              err = 1;
          }
          if (!have || err)
            fprintf(stderr,
                    "Invalid --join-indexes value (%s): must be a comma-separated"
                    " list of indexes with no spaces or other characters\n",
                    arg),
              err = 1;
          else {
            asprintf(&data.join_indexes, ",%s,", arg);
            if (data.join_indexes && strstr(data.join_indexes, ",0,"))
              fprintf(stderr, "--join-indexes index values must be greater than zero\n"), err = 1;
          }
        }
      } else if (!my_sql && ((*arg == '@' && arg[1]) || is_select_sql(arg))) {
        if (is_select_sql(arg))
          my_sql = arg;
        else {
          struct stat st;
          if (stat(arg + 1, &st) == 0 && st.st_size > 0) {
            FILE *f = fopen(arg + 1, "rb");
            if (f) {
              data.sql_dynamic = malloc(st.st_size + 1);
              if (data.sql_dynamic) {
                fread(data.sql_dynamic, 1, st.st_size, f);
                data.sql_dynamic[st.st_size] = '\0';
                if (is_select_sql(data.sql_dynamic))
                  my_sql = (const char *)data.sql_dynamic;
                else {
                  fprintf(stderr, "File %s contents must be a sql SELECT statement\n", arg + 1);
                  err = 1;
                }
              }
              fclose(f);
            }
          }
          if (!data.sql_dynamic && !err) {
            fprintf(stderr, "File %s empty or not readable\n", arg + 1);
            err = 1;
          }
        }
      } else if (!strcmp(arg, "-o") || !strcmp(arg, "--output"))
        writer_opts.output_path = zsv_next_arg(++arg_i, argc, argv, &err);
      else if (!strcmp(arg, "--memory"))
        data.in_memory = 1;
      else if (!strcmp(arg, "-b"))
        writer_opts.with_bom = 1;
      else if (*arg != '-') {
        if (!data.input_filename) {
          data.input_filename = arg;
          if (!(data.in = fopen(arg, "rb"))) {
            fprintf(stderr, "Unable to open for reading: %s\n", arg);
            err = 1;
          }
        } else { // another input file
          FILE *tmp_f;
          if (!(tmp_f = fopen(arg, "rb"))) {
            fprintf(stderr, "Unable to open %s for reading\n", arg);
            err = 1;
          } else {
            fclose(tmp_f);
            struct string_list *tmp = calloc(1, sizeof(**next_input_filename));
            if (!tmp)
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

    if (!data.in || !data.input_filename) {
#ifdef NO_STDIN
      fprintf(stderr, "Please specify an input file\n");
      err = 1;
#else
      data.in = stdin;
#endif
    }

    if (!my_sql && !data.join_indexes) {
      fprintf(stderr, "No sql command specified\n");
      err = 1;
    }

    if (err) {
      zsv_sql_cleanup(&data);
      return 1;
    }

    if (data.in != stdin) {
      fclose(data.in);
      data.in = NULL;
    }

    FILE *f = NULL;
    char *tmpfn = NULL;
    if (data.input_filename) {
      f = fopen(data.input_filename, "rb");
      if (!f)
        fprintf(stderr, "Unable to open %s for reading\n", data.input_filename);
    } else
      f = stdin;

    if (f == stdin) {
      tmpfn = zsv_get_temp_filename("zsv_sql_XXXXXXXX");
      if (!tmpfn) {
        fprintf(stderr, "Unable to create temp file name\n");
      } else {
        FILE *tmpf = fopen(tmpfn, "wb");
        if (!tmpf)
          fprintf(stderr, "Unable to open temp file %s\n", tmpfn);
        else {
          char rbuff[1024];
          size_t bytes_read;
          while ((bytes_read = fread(rbuff, 1, sizeof(rbuff), stdin)))
            fwrite(rbuff, 1, bytes_read, tmpf);
          f = tmpf;
        }
      }
    }

    zsv_csv_writer cw = zsv_writer_new(&writer_opts);
    if (f && cw) {
      fclose(f); // to do: don't open in the first place
      f = NULL;

      unsigned char cw_buff[1024];
      zsv_writer_set_temp_buff(cw, cw_buff, sizeof(cw_buff));

      struct zsv_sqlite3_dbopts dbopts = {
        .in_memory = data.in_memory,
      };
      struct zsv_sqlite3_db *zdb = zsv_sqlite3_db_new(&dbopts);
      if (zdb && zdb->rc == SQLITE_OK) {
        const char *csv_filename = tmpfn ? (const char *)tmpfn : data.input_filename;

        // for simplicity, we assume the same opts and custom_prop_handler for every input
        // it may be desirable later to make this customizable for each input
        if (zsv_sqlite3_add_csv(zdb, csv_filename, opts, custom_prop_handler) == SQLITE_OK) {
          for (struct string_list *sl = data.more_input_filenames; sl; sl = sl->next)
            if (zsv_sqlite3_add_csv(zdb, sl->value, opts, custom_prop_handler) != SQLITE_OK)
              break;
        }

        if (zdb->rc == SQLITE_OK && data.join_indexes) { // get column names, and construct the sql
          // sql template:
          // select t1.*, t2.*, t3.* from t1 left join (select * from t2 group by a) t2 left join (select * from t3
          // group by a) t3 using(a);
          sqlite3_stmt *stmt = NULL;
          const char *prefix_search = NULL;
          const char *prefix_end = NULL;
          if (my_sql) {
            prefix_search = " from data ";
            prefix_end = strstr(my_sql, prefix_search);
            if (!prefix_end) {
              prefix_search = " from data";
              prefix_end = strstr(my_sql, prefix_search);
              if (prefix_end && (prefix_end + strlen(prefix_search) != my_sql + strlen(my_sql)))
                prefix_end = NULL;
            }
            if (!prefix_end || !prefix_search) {
              err = 1;
              fprintf(stderr, "Invalid sql: must contain 'from data'");
            }
          }

          if (!err) {
            zdb->rc = sqlite3_prepare_v2(zdb->db, "select * from data", -1, &stmt, NULL);
            if (zdb->rc != SQLITE_OK) {
              fprintf(stderr, "%s:\n  %s\n (or bad CSV/utf8 input)\n\n", sqlite3_errstr(err), "select * from data");
              err = 1;
            }
          }
          if (!err && stmt) {
            struct string_list **next_joined_column_name = &data.join_column_names;
            int col_count = sqlite3_column_count(stmt);
            for (char *ix_str = data.join_indexes; !err && ix_str && *ix_str && *(++ix_str);
                 ix_str = strchr(ix_str + 1, ',')) {
              unsigned int next_ix;
              if (sscanf(ix_str, "%u,", &next_ix) == 1) {
                if (next_ix == 0)
                  fprintf(stderr, "--join-indexes index must be greater than zero\n");
                else if (next_ix > (unsigned)col_count)
                  fprintf(stderr, "Column %u out of range; input has only %i columns\n", next_ix, col_count), err = 1;
                else if (!sqlite3_column_name(stmt, next_ix - 1))
                  fprintf(stderr, "Column %u unexpectedly missing name\n", next_ix);
                else {
                  struct string_list *tmp = calloc(1, sizeof(**next_joined_column_name));
                  if (!tmp)
                    fprintf(stderr, "Out of memory!\n"), err = 1;
                  else {
                    tmp->value = strdup(sqlite3_column_name(stmt, next_ix - 1));
                    *next_joined_column_name = tmp;
                    next_joined_column_name = &tmp->next;
                  }
                }
              }
            }

            if (!data.more_input_filenames)
              fprintf(stderr, "--join-indexes requires more than one input\n"), err = 1;
            else if (!err) { // now build the join select
              sqlite3_str *select_clause = sqlite3_str_new(zdb->db);
              sqlite3_str *from_clause = sqlite3_str_new(zdb->db);
              sqlite3_str *group_by_clause = sqlite3_str_new(zdb->db);

              sqlite3_str_appendf(select_clause, "data.*");
              sqlite3_str_appendf(from_clause, "data");

              for (struct string_list *sl = data.join_column_names; sl; sl = sl->next) {
                if (sl != data.join_column_names)
                  sqlite3_str_appendf(group_by_clause, ",");
                sqlite3_str_appendf(group_by_clause, "\"%w\"", sl->value);
              }

              int i = 2;
              for (struct string_list *sl = data.more_input_filenames; sl; sl = sl->next, i++) {
                sqlite3_str_appendf(select_clause, ", data%i.*", i);
                // left join (select * from t2 group by a) t2 using(x,...)
                sqlite3_str_appendf(from_clause, " left join (select * from data%i group by %s) data%i", i,
                                    sqlite3_str_value(group_by_clause), i);
                sqlite3_str_appendf(from_clause, " using (%s)", sqlite3_str_value(group_by_clause));
              }

              if (!prefix_end || !prefix_search)
                asprintf(&data.sql_dynamic, "select %s from %s", sqlite3_str_value(select_clause),
                         sqlite3_str_value(from_clause));
              else {
                asprintf(&data.sql_dynamic, "%.*s from %s%s%s", (int)(prefix_end - my_sql), my_sql,
                         sqlite3_str_value(from_clause), strlen(prefix_end + strlen(prefix_search)) ? " " : "",
                         strlen(prefix_end + strlen(prefix_search)) ? prefix_end + strlen(prefix_search) : "");
              }

              my_sql = data.sql_dynamic;
              if (opts->verbose)
                fprintf(stderr, "Join sql:\n%s\n", my_sql);
              sqlite3_free(sqlite3_str_finish(select_clause));
              sqlite3_free(sqlite3_str_finish(from_clause));
              sqlite3_free(sqlite3_str_finish(group_by_clause));
            }
          }
          if (stmt)
            sqlite3_finalize(stmt);
        }

        if (zdb->rc == SQLITE_OK && !err && my_sql) {
          sqlite3_stmt *stmt;
          err = sqlite3_prepare_v2(zdb->db, my_sql, -1, &stmt, NULL);
          if (err != SQLITE_OK)
            fprintf(stderr, "%s:\n  %s\n (or bad CSV/utf8 input)\n\n", sqlite3_errstr(err), my_sql);
          else {

            int col_count = sqlite3_column_count(stmt);

            // write header row
            for (int i = 0; i < col_count; i++) {
              const char *colname = sqlite3_column_name(stmt, i);
              zsv_writer_cell(cw, !i, (const unsigned char *)colname, colname ? strlen(colname) : 0, 1);
            }

            // write sql results
            while (sqlite3_step(stmt) == SQLITE_ROW) {
              for (int i = 0; i < col_count; i++) {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                int len = text ? sqlite3_column_bytes(stmt, i) : 0;
                zsv_writer_cell(cw, !i, text, len, 1);
              }
            }
            sqlite3_finalize(stmt);
          }
        }
        err = 1;
        if (zdb->err_msg)
          fprintf(stderr, "Error: %s\n", zdb->err_msg);
        else if (!zdb->db)
          fprintf(stderr, "Error (unable to open db, code %i): %s\n", zdb->rc, sqlite3_errstr(zdb->rc));
        else if (zdb->rc != SQLITE_OK)
          fprintf(stderr, "Error (code %i): %s\n", zdb->rc, sqlite3_errstr(zdb->rc));
        else
          err = 0;

        zsv_sqlite3_db_delete(zdb);
      }
    }
    if (f)
      fclose(f);
    zsv_sql_finalize(&data);
    zsv_sql_cleanup(&data);
    zsv_writer_delete(cw);

    if (tmpfn) {
      unlink(tmpfn);
      free(tmpfn);
    }
  }
  return err;
}
