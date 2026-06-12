#ifndef ZSV_UTILS_SQL_H
#define ZSV_UTILS_SQL_H

typedef struct zsv_sqlite3_db *zsv_sqlite3_db_t;

struct zsv_sqlite3_dbopts {
  unsigned char in_memory : 1;
  unsigned char dedupe_cols : 1;      // auto-rename duplicate input column names (a, a_2, ...)
  unsigned char warn_dupe_rename : 1; // note each such rename on stderr (off for curses UIs)
  unsigned char _ : 5;
};

#endif
