/* clang-format off */
/*
 * This file has been modified from its original form, in order to use the ZSV csv parser
 * The preamble / disclaimer to the original file is included below
 * The modifications to this file are subject to the same license (MIT) as the ZSV parser
 * as described at https://github.com/liquidaty/zsv/blob/main/LICENSE
 */

/*
** 2016-05-28
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file contains the implementation of an SQLite virtual table for
** reading CSV files
**
** Usage:
**
**    .load ./csv
**    CREATE VIRTUAL TABLE temp.csv USING csv(filename=FILENAME);
**    SELECT * FROM csv;
**
** The input file is assumed to have a single header row, followed by data rows
** Instead of specifying a file, the text of the CSV can be loaded using
** the data= parameter.
**
** If the columns=N parameter is supplied, then the CSV file is assumed to have
** N columns.  If both the columns= and schema= parameters are omitted, then
** the number and names of the columns is determined by the first line of
** the CSV input.
**
** Some extra debugging features (used for testing virtual tables) are available
** if this module is compiled with -DSQLITE_TEST.
*/
#include "sqlite3.h"
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <zsv.h>
#include <zsv/utils/string.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/prop.h>
#include "sqlite3_csv_vtab-mem.c"

#ifndef SQLITE_OMIT_VIRTUALTABLE

/*
** A macro to hint to the compiler that a function should not be
** inlined.
*/
#if defined(__GNUC__)
#  define CSV_NOINLINE  __attribute__((noinline))
#elif defined(_MSC_VER) && _MSC_VER>=1310
#  define CSV_NOINLINE  __declspec(noinline)
#else
#  define CSV_NOINLINE
#endif


/* Max size of the error message in a CsvReader */
#define CSV_MXERR 200

/* Size of the CsvReader input buffer */
#define CSV_INBUFSZ 1024

/* Forward references to the various virtual table methods implemented
** in this file. */
static int zsvtabCreate(sqlite3*, void*, int, const char*const*,
                           sqlite3_vtab**,char**);
static int zsvtabConnect(sqlite3*, void*, int, const char*const*,
                           sqlite3_vtab**,char**);
static int zsvtabBestIndex(sqlite3_vtab*,sqlite3_index_info*);
static int zsvtabDisconnect(sqlite3_vtab*);
static int zsvtabOpen(sqlite3_vtab*, sqlite3_vtab_cursor**);
static int zsvtabClose(sqlite3_vtab_cursor*);
static int zsvtabFilter(sqlite3_vtab_cursor*, int idxNum, const char *idxStr,
                          int argc, sqlite3_value **argv);
static int zsvtabNext(sqlite3_vtab_cursor*);
static int zsvtabEof(sqlite3_vtab_cursor*);
static int zsvtabColumn(sqlite3_vtab_cursor*,sqlite3_context*,int);
static int zsvtabRowid(sqlite3_vtab_cursor*,sqlite3_int64*);

/* An instance of the CSV virtual table.
** Holds only configuration: each scan runs on a cursor-owned parser (see
** zsvCursor), so the table itself keeps no live parser or file handle and is
** safe to share across the concurrent cursors SQLite opens for self joins,
** correlated subqueries, etc. */
typedef struct zsvTable {
  sqlite3_vtab base;              /* Base class.  Must be first */
  char *zFilename;                /* Name of the CSV file */
  struct zsv_opts parser_opts;    /* template; per-cursor copies set .stream */
  struct zsv_prop_handler custom_prop_handler;
} zsvTable;

struct zsvTable *zsvTable_new(const char *filename) {
  struct zsvTable *z = sqlite3_malloc(sizeof(*z));
  if(z) {
    memset(z, 0, sizeof(*z));
    struct sqlite3_zsv_data *d = sqlite3_zsv_data_find(filename);
    if(d) {
      z->parser_opts = d->opts; // zsv_get_default_opts();
      z->custom_prop_handler = d->custom_prop_handler; // zsv_get_default_custom_prop_handler();
    }
  }
  return z;
}

/* Allowed values for tstFlags */
#define CSVTEST_FIDX  0x0001      /* Pretend that constrained searchs cost less*/

/* A cursor for the CSV virtual table. Each cursor owns an independent parser and
** file handle so concurrent scans over the same table maintain separate scan
** state and never free memory another cursor still references. */
typedef struct zsvCursor {
  sqlite3_vtab_cursor base;       /* Base class.  Must be first */
  FILE *stream;                   /* this cursor's own handle on the CSV file */
  zsv_parser parser;
  enum zsv_status parser_status;
  sqlite_int64 rowCount;
} zsvCursor;

/*
** The xConnect and xCreate methods do the same thing, but they must be
** different so that the virtual table is not an eponymous virtual table.
o*/
static int zsvtabCreate(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
 return zsvtabConnect(db, pAux, argc, argv, ppVtab, pzErr);
}

static void zsvTable_delete(struct zsvTable *z) {
  if(z) {
    sqlite3_free(z->zFilename);
    sqlite3_free(z);
  }
}

/* Release a cursor's parser and file handle. Idempotent: safe to call on a
** partially-initialized or already-freed cursor. */
static void zsvCursor_free(struct zsvCursor *cur) {
  if(cur->parser) {
    zsv_delete(cur->parser);
    cur->parser = NULL;
  }
  if(cur->stream) {
    fclose(cur->stream);
    cur->stream = NULL;
  }
  cur->rowCount = 0;
}

/* Open pTab's file and construct a parser over it using pTab's saved options.
** On success *pStream and *pParser are set and owned by the caller; on failure
** both are left NULL (the stream, if opened, is closed here) and SQLITE_ERROR is
** returned. Shared by zsvCursor_init (per-cursor scan) and zsvtabConnect (a
** throwaway header read); each caller does its own zsv_next_row handling after,
** since that part diverges (stop-at-header vs skip-header-then-advance). */
static int zsvOpenParser(struct zsvTable *pTab, FILE **pStream, zsv_parser *pParser) {
  *pStream = NULL;
  *pParser = NULL;
  FILE *stream = fopen(pTab->zFilename, "rb");
  if(!stream)
    return SQLITE_ERROR;
  struct zsv_opts opts = pTab->parser_opts;
  opts.stream = stream;
  if(zsv_new_with_properties(&opts, &pTab->custom_prop_handler, pTab->zFilename, pParser) != zsv_status_ok) {
    fclose(stream);
    *pParser = NULL;
    return SQLITE_ERROR;
  }
  *pStream = stream;
  return SQLITE_OK;
}

/* Open a fresh parser for this cursor, positioned at the first data row.
** Skips the header row exactly as zsvtabConnect does when building the schema. */
static int zsvCursor_init(struct zsvCursor *cur, struct zsvTable *pTab) {
  if(zsvOpenParser(pTab, &cur->stream, &cur->parser) != SQLITE_OK)
    return SQLITE_ERROR;
  if((cur->parser_status = zsv_next_row(cur->parser)) != zsv_status_row) /* header */
    return SQLITE_ERROR;
  cur->parser_status = zsv_next_row(cur->parser);                        /* first data row */
  cur->rowCount = 1;
  return SQLITE_OK;
}

#include "vtab_helper.c"

#define BLANK_COLUMN_NAME_PREFIX "Blank_Column"
unsigned blank_column_name_count = 0;

/* Return the 0-based index of the first of the `n` already-chosen column names
** that equals `name` under SQLite's identifier comparison (ASCII
** case-insensitive), or -1 if none. Using SQLite's own comparison means
** disambiguation is guaranteed to be accepted and detection matches exactly
** what SQLite would otherwise reject. */
static long zsv_csv_colname_find(char *const *seen, size_t n, const char *name) {
  for(size_t i = 0; i < n; i++)
    if(seen[i] && sqlite3_stricmp(seen[i], name) == 0)
      return (long)i;
  return -1;
}

/* Append the `"col" TEXT, ...` specifications for each header cell to pStr.
** When `dedupe` is nonzero, duplicate column names are made unique by appending
** _2, _3, ... so input with repeated headers is loadable: the first occurrence of
** a name keeps it and later occurrences are renamed. A synthesized name is probed
** against every user-supplied name and every name already assigned, so it never
** displaces an explicit column (a,a,a_2 -> a,a_3,a_2). If `warn` is nonzero, one
** summary line naming up to 5 renames is printed to stderr (never silent, but
** collapsed to a single line); callers driving a curses UI (sheet) pass warn=0.
** When `dedupe` is zero, the first duplicate name is reported via *dup_errmsg
** (allocated with asprintf, naming the column and its 1-based positions) so the
** caller can fail with an actionable message instead of SQLite's generic one.
** Returns 0 on success, 1 if a duplicate was reported, -1 on out-of-memory. */
static int zsv_csv_append_column_defs(sqlite3_str *pStr, zsv_parser parser, int dedupe, int warn, char **dup_errmsg) {
  size_t ncols = zsv_cell_count(parser);
  char **base = NULL;  // user-supplied name (trimmed, or blank placeholder) per column
  char **final = NULL; // unique name assigned to each column (may alias base[i])
  sqlite3_str *summary = NULL;
  unsigned nrenamed = 0;
  int rc = 0;
  if(ncols) {
    base = sqlite3_malloc64((sqlite3_int64)(sizeof(*base) * ncols));
    final = sqlite3_malloc64((sqlite3_int64)(sizeof(*final) * ncols));
    if(!base || !final) {
      rc = -1;
      goto done;
    }
    memset(base, 0, sizeof(*base) * ncols);
    memset(final, 0, sizeof(*final) * ncols);
  }

  // phase 1: capture every column's user-supplied name; blanks keep their placeholder
  for(size_t i = 0; i < ncols; i++) {
    struct zsv_cell cell = zsv_get_cell(parser, i);
    size_t len = cell.len;
    unsigned char *utf8_value = (unsigned char *)zsv_strtrim(cell.str, &len);
    if(!len) {
      if(blank_column_name_count++)
        base[i] = sqlite3_mprintf("%s_%u", BLANK_COLUMN_NAME_PREFIX, blank_column_name_count - 1);
      else
        base[i] = sqlite3_mprintf("%s", BLANK_COLUMN_NAME_PREFIX);
    } else
      base[i] = sqlite3_mprintf("%.*s", (int)len, utf8_value);
    if(!base[i]) {
      rc = -1;
      goto done;
    }
  }

  // phase 2: assign a unique name to each column and emit its column def
  for(size_t i = 0; i < ncols; i++) {
    if(!dedupe) {
      long prior = zsv_csv_colname_find(base, i, base[i]);
      if(prior >= 0) {
        if(asprintf(dup_errmsg, "duplicate column name \"%s\" at columns %d and %d", base[i], (int)(prior + 1),
                    (int)(i + 1)) < 0)
          *dup_errmsg = NULL;
        rc = *dup_errmsg ? 1 : -1;
        goto done;
      }
      final[i] = base[i]; // no dedupe: name is already unique (borrows base[i])
    } else if(zsv_csv_colname_find(base, i, base[i]) < 0) {
      final[i] = base[i]; // first occurrence keeps its name (borrows base[i])
    } else {
      // duplicate: bump _k past every user-supplied name and every name already assigned,
      // so disambiguation never collides with, or displaces, an explicit column
      char *cand = NULL;
      for(unsigned k = 2;; k++) {
        sqlite3_free(cand);
        cand = sqlite3_mprintf("%s_%u", base[i], k);
        if(!cand) {
          rc = -1;
          goto done;
        }
        if(zsv_csv_colname_find(base, ncols, cand) < 0 && zsv_csv_colname_find(final, i, cand) < 0)
          break;
      }
      final[i] = cand;
      if(warn) {
        if(!summary && !(summary = sqlite3_str_new(0))) {
          rc = -1;
          goto done;
        }
        if(nrenamed < 5)
          sqlite3_str_appendf(summary, "%s%s\xe2\x86\x92%s", nrenamed ? ", " : "", base[i], final[i]);
        nrenamed++;
      }
    }
    sqlite3_str_appendf(pStr, "%s\"%w\" TEXT", i > 0 ? "," : "", final[i]);
  }

  if(warn && nrenamed) {
    if(nrenamed > 5)
      sqlite3_str_appendf(summary, ", +%u more", nrenamed - 5);
    fprintf(stderr,
            "note: auto-renamed %u duplicate input column(s): %s"
            "  (pass --error-on-duplicate-columns to treat as an error)\n",
            nrenamed, sqlite3_str_value(summary));
  }

done:
  if(final) {
    for(size_t i = 0; i < ncols; i++)
      if(final[i] && final[i] != base[i]) // only synthesized names are owned separately
        sqlite3_free(final[i]);
    sqlite3_free(final);
  }
  if(base) {
    for(size_t i = 0; i < ncols; i++)
      sqlite3_free(base[i]);
    sqlite3_free(base);
  }
  if(summary)
    sqlite3_free(sqlite3_str_finish(summary));
  return rc;
}

/**
 * Parameters:
 *    filename=FILENAME          Name of file containing CSV content
 *
 * The number of columns in the first row of the input file determines the
 * column names and column count
 */
static int zsvtabConnect(
  sqlite3 *db,
  void *_pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  (void)(_pAux);
  zsvTable pTmp = { 0 };
  int rc = SQLITE_OK;        /* Result code from this routine */
  #define ZSVTABCONNECT_PARAM_MAX 3
  static const char *azParam[ZSVTABCONNECT_PARAM_MAX] = {
     "filename", "dedupe", "warn_renames"
  };
  char *azPValue[ZSVTABCONNECT_PARAM_MAX]; /* Parameter values */
  memset(azPValue, 0, sizeof(azPValue));
# define CSV_FILENAME     (azPValue[0])
# define CSV_DEDUPE       (azPValue[1])
# define CSV_WARN_RENAMES (azPValue[2])

  char *schema = NULL;
  zsvTable *pNew = NULL;
  zsv_parser hdr_parser = NULL; /* throwaway: reads the header row to build the schema */
  FILE *hdr_stream = NULL;

  char *errmsg = NULL;
  // set parameters
  for(int i=3; i<argc; i++){
    const char *z = argv[i];
    const char *zValue;
    size_t j;
    for(j=0; j<sizeof(azParam)/sizeof(azParam[0]); j++){
      if(csv_string_parameter(&errmsg, azParam[j], z, &azPValue[j]) ) break;
    }
    if( j<sizeof(azParam)/sizeof(azParam[0]) ){
      if( errmsg ) goto zsvtab_connect_error;
    } else {
      asprintf(&errmsg, "bad parameter: '%s'", z);
      goto zsvtab_connect_error;
    }
  }

  if(!CSV_FILENAME) {
    asprintf(&errmsg, "No csv filename provided");
    goto zsvtab_connect_error;
  }

  pNew = zsvTable_new(CSV_FILENAME);
  if(!pNew)
    goto zsvtab_connect_oom;
  if(pTmp.parser_opts.max_columns)
    pNew->parser_opts.max_columns = pTmp.parser_opts.max_columns;
  else if(!pNew->parser_opts.max_columns)
    pNew->parser_opts.max_columns = 2000; /* default max columns */

  pNew->zFilename = CSV_FILENAME;
  CSV_FILENAME = 0; // in use; don't free

  // Read the header row with a throwaway parser (each scan uses its own
  // cursor-owned parser, so the table holds no live parser or file handle).
  if(zsvOpenParser(pNew, &hdr_stream, &hdr_parser) != SQLITE_OK) {
    asprintf(&errmsg, "Unable to open for reading: %s", pNew->zFilename);
    goto zsvtab_connect_error;
  }

  enum zsv_status hdr_status = zsv_next_row(hdr_parser);
  if(hdr_status != zsv_status_row) {
    asprintf(&errmsg, "Could not fetch header row: %s", zsv_parse_status_desc(hdr_status));
    goto zsvtab_connect_error;
  }

  *ppVtab = (sqlite3_vtab*)pNew;

  // generate the CREATE TABLE statement. When `dedupe` is requested, repeated
  // column names are disambiguated (a,b,a -> a,b,a_2) so the table is loadable.
  int do_dedupe = CSV_DEDUPE && atoi(CSV_DEDUPE) != 0;
  int do_warn = CSV_WARN_RENAMES && atoi(CSV_WARN_RENAMES) != 0;
  sqlite3_str *pStr = sqlite3_str_new(0);
  sqlite3_str_appendf(pStr, "CREATE TABLE x(");
  int defstat = zsv_csv_append_column_defs(pStr, hdr_parser, do_dedupe, do_warn, &errmsg);
  if(defstat) {
    sqlite3_free(sqlite3_str_finish(pStr));
    if(defstat > 0) // duplicate column name; errmsg holds an actionable message
      goto zsvtab_connect_error;
    goto zsvtab_connect_oom;
  }
  sqlite3_str_appendf(pStr, ")");
  schema = sqlite3_str_finish(pStr);
  if(!schema)
    goto zsvtab_connect_oom;

#ifdef SQLITE_TEST
  pNew->tstFlags = tstFlags;
#endif

  rc = sqlite3_declare_vtab(db, schema);
  if( rc ){
    asprintf(&errmsg, "bad schema: '%s' - %s", schema, sqlite3_errmsg(db));
    goto zsvtab_connect_error;
  }
  for(unsigned int i=0; i<sizeof(azPValue)/sizeof(azPValue[0]); i++) {
    sqlite3_free(azPValue[i]);
  }
  sqlite3_free(schema);
  zsv_delete(hdr_parser);
  fclose(hdr_stream);

  /* Rationale for DIRECTONLY:
  ** An attacker who controls a database schema could use this vtab
  ** to exfiltrate sensitive data from other files in the filesystem.
  ** And, recommended practice is to put all CSV virtual tables in the
  ** TEMP namespace, so they should still be usable from within TEMP
  ** views, so there shouldn't be a serious loss of functionality by
  ** prohibiting the use of this vtab from persistent triggers and views.
  */
  sqlite3_vtab_config(db, SQLITE_VTAB_DIRECTONLY);
  return SQLITE_OK;

zsvtab_connect_oom:
  rc = SQLITE_NOMEM;
  asprintf(&errmsg, "Out of memory!");

zsvtab_connect_error:
  if(hdr_parser) zsv_delete(hdr_parser);
  if(hdr_stream) fclose(hdr_stream);
  if( pNew ) zsvtabDisconnect(&pNew->base);
  for(unsigned int i=0; i<sizeof(azPValue)/sizeof(azPValue[0]); i++){
    sqlite3_free(azPValue[i]);
  }
  sqlite3_free(schema);
  if(errmsg) {
    sqlite3_free(*pzErr);
    *pzErr = sqlite3_mprintf("%s", errmsg);
    free(errmsg);
  }
  if( rc==SQLITE_OK ) rc = SQLITE_ERROR;
  return rc;
}

/*
** Only a forward full table scan is supported.  xBestIndex is mostly
** a no-op.  If CSVTEST_FIDX is set, then the presence of equality
** constraints lowers the estimated cost, which is fiction, but is useful
** for testing certain kinds of virtual table behavior.
*/
static int zsvtabBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  (void)(tab);
  pIdxInfo->estimatedCost = 1000000;
  return SQLITE_OK;
}

/*
** This method is the destructor for a zsvTable object.
*/
static int zsvtabDisconnect(sqlite3_vtab *pVtab){
  zsvTable *p = (zsvTable*)pVtab;
  zsvTable_delete(p);
  return SQLITE_OK;
}

/*
** Constructor for a new zsvTable cursor object.
*/
static int zsvtabOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  (void)(p);
  struct zsvCursor *pCur = sqlite3_malloc64(sizeof(*pCur));
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a zsvCursor.
*/
static int zsvtabClose(sqlite3_vtab_cursor *cur){
  zsvCursor_free((struct zsvCursor*)cur);
  sqlite3_free(cur);
  return SQLITE_OK;
}

/*
** Only a full table scan is supported.  So xFilter simply (re)opens this
** cursor's own parser at the start of the file.
*/
static int zsvtabFilter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  (void)(idxNum);
  (void)(idxStr);
  (void)(argc);
  (void)(argv);
  struct zsvCursor *pCur = (struct zsvCursor*)pVtabCursor;
  zsvTable *pTab = (zsvTable*)pVtabCursor->pVtab;

  zsvCursor_free(pCur); // discard any prior scan (xFilter may be called repeatedly)
  int rc = zsvCursor_init(pCur, pTab);
  if(rc != SQLITE_OK)
    zsvCursor_free(pCur);
  return rc;
}


/*
** Advance a zsvCursor to its next row of input.
** Set the EOF marker via the cursor's parser_status if we reach end of input.
*/
static int zsvtabNext(sqlite3_vtab_cursor *cur){
  struct zsvCursor *pCur = (struct zsvCursor*)cur;
  pCur->parser_status = zsv_next_row(pCur->parser);
  pCur->rowCount++;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int zsvtabEof(sqlite3_vtab_cursor *cur){
  struct zsvCursor *pCur = (struct zsvCursor*)cur;
  return pCur->parser_status != zsv_status_row;
}

/*
** Return values of columns for the row at which the zsvCursor
** is currently pointing.
*/
static int zsvtabColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  struct zsvCursor *pCur = (struct zsvCursor*)cur;
  struct zsv_cell c = zsv_get_cell(pCur->parser, i);
  // SQLITE_STATIC is safe: the cell points into this cursor's own parser buffer,
  // which stays valid until this cursor advances (zsvtabNext) or closes -- no
  // other cursor can free it.
  sqlite3_result_text(ctx, (char *)c.str, c.len, SQLITE_STATIC);
  return SQLITE_OK;
}


/*
** Return the rowid for the current row.
*/
static int zsvtabRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  struct zsvCursor *pCur = (struct zsvCursor*)cur;
  *pRowid = pCur->rowCount;
  return SQLITE_OK;
}

sqlite3_module CsvModule = {
  0,                       /* iVersion */
  zsvtabCreate,            /* xCreate */
  zsvtabConnect,           /* xConnect */
  zsvtabBestIndex,         /* xBestIndex */
  zsvtabDisconnect,        /* xDisconnect */
  zsvtabDisconnect,        /* xDestroy */
  zsvtabOpen,              /* xOpen - open a cursor */
  zsvtabClose,             /* xClose - close a cursor */
  zsvtabFilter,            /* xFilter - configure scan constraints */
  zsvtabNext,              /* xNext - advance a cursor */
  zsvtabEof,               /* xEof - check for end of scan */
  zsvtabColumn,            /* xColumn - read data */
  zsvtabRowid,             /* xRowid - read data */
  0,                       /* xUpdate */
  0,                       /* xBegin */
  0,                       /* xSync */
  0,                       /* xCommit */
  0,                       /* xRollback */
  0,                       /* xFindMethod */
  0,                       /* xRename */
  0, 0, 0, 0 /* xSavepoint, xRelease, xRollbackTo, xShadowName */
};

#endif /* !defined(SQLITE_OMIT_VIRTUALTABLE) */


#ifdef _WIN32
__declspec(dllexport)
#endif
/*
** This routine is called when the extension is loaded.  The new
** CSV virtual table module is registered with the calling database
** connection.
*/
int sqlite3_csv_init(
  sqlite3 *db,
  char **pzErrMsg,
  const sqlite3_api_routines *pApi
){
  (void)(pzErrMsg);
#ifndef SQLITE_OMIT_VIRTUALTABLE
  int rc;
  SQLITE_EXTENSION_INIT2(pApi);
  pthread_mutex_t init = PTHREAD_MUTEX_INITIALIZER;
  memcpy(&sqlite3_zsv_data_mutex, &init, sizeof(init));
  rc = sqlite3_create_module_v2(db, "csv", &CsvModule, &sqlite3_zsv_data_g, (void (*)(void *))sqlite3_zsv_list_delete);
#ifdef SQLITE_TEST
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_module(db, "csv_wr", &CsvModuleFauxWrite, 0);
  }
#endif
  return rc;
#else
  return SQLITE_OK;
#endif
}
/* clang-format on */
