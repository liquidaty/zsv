#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <jsonwriter.h>

int zsv_dbtable2json(sqlite3 *db, const char *tname, jsonwriter_handle jsw) {
  int err = 0;
  const char *index_sql = "select name, sql from sqlite_master where type = 'index' and tbl_name = :tbl_name";
  const char *unique_sql = "select 1 from PRAGMA_index_list(?) where name = ? and [unique] <> 0";
  sqlite3_str *data_sql = sqlite3_str_new(db);
  if(data_sql) {
    sqlite3_str_appendf(data_sql, "select * from \"%w\"", tname);
    sqlite3_stmt *data_stmt = NULL;
    sqlite3_stmt *index_stmt = NULL;
    sqlite3_stmt *unique_stmt = NULL;
    int colcount = 0;

    err = 1;
    if(sqlite3_prepare_v2(db, sqlite3_str_value(data_sql), -1, &data_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", sqlite3_str_value(data_sql), sqlite3_errmsg(db));
    else if(!(colcount = sqlite3_column_count(data_stmt)))
      fprintf(stderr, "No columns found in table %s\n", tname);
    else if(sqlite3_prepare_v2(db, index_sql, -1, &index_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", index_sql, sqlite3_errmsg(db));
    else if(sqlite3_prepare_v2(db, unique_sql, -1, &unique_stmt, NULL) != SQLITE_OK)
      fprintf(stderr, "Unable to prepare %s: %s\n", unique_sql, sqlite3_errmsg(db));
    else {
      err = 0;
      jsonwriter_start_array(jsw); // output is an array with 2 items: meta and data

      // ----- meta: columns and index info
      jsonwriter_start_object(jsw);

      jsonwriter_object_cstr(jsw, "name", tname);

      jsonwriter_object_array(jsw, "columns"); // columns
      for(int i = 0; i < colcount; i++) {
        const char *colname = sqlite3_column_name(data_stmt, i);
        jsonwriter_start_object(jsw);
        jsonwriter_object_cstr(jsw, "name", colname);
        const char *dtype = sqlite3_column_decltype(data_stmt, i);
        if(dtype)
          jsonwriter_object_cstr(jsw, "datatype", dtype);

        // to do: collate nocase etc
        jsonwriter_end_object(jsw);
      }
      jsonwriter_end_array(jsw); // end columns

      // indexes
      jsonwriter_object_object(jsw, "indexes"); // indexes
      sqlite3_bind_text(index_stmt, 1, tname, (int)strlen(tname), SQLITE_STATIC);
      while(sqlite3_step(index_stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(index_stmt, 0);
        const unsigned char *ix_sql = sqlite3_column_text(index_stmt, 1);
        size_t len = text ? sqlite3_column_bytes(index_stmt, 0) : 0;
        size_t ix_sql_len = ix_sql ? sqlite3_column_bytes(index_stmt, 1) : 0;


        if(text && ix_sql && len && ix_sql_len) {
          // on: for now we just look for the first and last parens
          const unsigned char *first_paren = memchr(ix_sql, '(', ix_sql_len);
          const unsigned char *last_paren = ix_sql + ix_sql_len;
          while(first_paren && last_paren > first_paren + 1 && *last_paren != ')')
            last_paren--;
          if(first_paren && last_paren > first_paren) {
            // name
            jsonwriter_object_keyn(jsw, (const char *)text, len);

            // ix obj
            jsonwriter_start_object(jsw);

            // unique
            sqlite3_bind_text(unique_stmt, 1, tname, (int)strlen(tname), SQLITE_STATIC);
            sqlite3_bind_text(unique_stmt, 2, (const char *)text, len, SQLITE_STATIC);
            if(sqlite3_step(unique_stmt) == SQLITE_ROW)
              jsonwriter_object_bool(jsw, "unique", 1);
            sqlite3_reset(unique_stmt);

            // on
            jsonwriter_object_strn(jsw, "on", first_paren + 1, last_paren - first_paren - 1);

            // end ix obj
            jsonwriter_end_object(jsw);
          }
        }
      }
      jsonwriter_end_object(jsw); // end indexes

      jsonwriter_end_object(jsw); // end meta obj

      // ------ data: array of rows
      jsonwriter_start_array(jsw);
      // for each row
      while(sqlite3_step(data_stmt) == SQLITE_ROW) {
        jsonwriter_start_array(jsw); // start row
        for(int i = 0; i < colcount; i++) {
          const unsigned char *text = sqlite3_column_text(data_stmt, i);
          if(text) {
            int len = sqlite3_column_bytes(data_stmt, i);
            jsonwriter_strn(jsw, text, len);
          } else
            jsonwriter_null(jsw);
        }
        jsonwriter_end_array(jsw); // end row
        //        sqlite3_reset(data_stmt);
      }
      jsonwriter_end_array(jsw);

      jsonwriter_end_array(jsw); // end of output
    }
    if(data_stmt)
      sqlite3_finalize(data_stmt);
    if(index_stmt)
      sqlite3_finalize(index_stmt);
    if(unique_stmt)
      sqlite3_finalize(unique_stmt);

    sqlite3_free(sqlite3_str_finish(data_sql));
  } // if data_sql
  return err;
}
