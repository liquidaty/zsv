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

/* An instance of the CSV virtual table */
typedef struct zsvTable {
  sqlite3_vtab base;              /* Base class.  Must be first */
  char *zFilename;                /* Name of the CSV file */
  struct zsv_opts parser_opts;
  struct zsv_prop_handler custom_prop_handler;
  char *opts_used;
  enum zsv_status parser_status;
  zsv_parser parser;
  sqlite_int64 rowCount;
} zsvTable;

struct zsvTable *zsvTable_new() {
  struct zsvTable *z = sqlite3_malloc(sizeof(*z));
  if(z) {
    memset(z, 0, sizeof(*z));
    z->parser_opts = zsv_get_default_opts();
    z->custom_prop_handler = zsv_get_default_custom_prop_handler();
  }
  return z;
}

/* Allowed values for tstFlags */
#define CSVTEST_FIDX  0x0001      /* Pretend that constrained searchs cost less*/

/* A cursor for the CSV virtual table */
typedef struct zsvCursor {
  sqlite3_vtab_cursor base;       /* Base class.  Must be first */
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

static void zsvTable_free(struct zsvTable *z) {
  if(z->parser)
    zsv_delete(z->parser);
  z->parser = NULL;
  z->rowCount = 0;
}

static void zsvTable_delete(struct zsvTable *z) {
  if(z) {
    zsvTable_free(z);
    sqlite3_free(z->zFilename);
    sqlite3_free(z->opts_used);
    sqlite3_free(z);
  }
}

#include "vtab_helper.c"

#define BLANK_COLUMN_NAME_PREFIX "Blank_Column"
unsigned blank_column_name_count = 0;

/**
 * Parameters:
 *    filename=FILENAME          Name of file containing CSV content
 *    options_used=OPTIONS_USED  Used options (passed to zsv_new_with_properties())
 *    max_columns=N              Error out if we encounter more cols than this
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
  zsvTable *pNew = NULL;
  int rc = SQLITE_OK;        /* Result code from this routine */
  #define ZSVTABCONNECT_PARAM_MAX 3
  static const char *azParam[ZSVTABCONNECT_PARAM_MAX] = {
     "filename", "options_used", "max_columns"
  };
  char *azPValue[ZSVTABCONNECT_PARAM_MAX]; /* Parameter values */
# define CSV_FILENAME (azPValue[0])
# define ZSV_OPTS_USED (azPValue[1])

  char *schema = NULL;
  pNew = zsvTable_new();
  if(!pNew)
    return SQLITE_NOMEM;

  pNew->parser_opts.max_columns = 2000; /* default max columns */

  (void)(_pAux);
  memset(azPValue, 0, sizeof(azPValue));

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
    }else
      // optional values
    if( (zValue = csv_parameter("max_columns",11,z))!=0 ){
      pNew->parser_opts.max_columns = atoi(zValue);
      if(pNew->parser_opts.max_columns<=0 || pNew->parser_opts.max_columns > 2000){
        asprintf(&errmsg, "max_columns= value must be > 0 and < 2000");
        goto zsvtab_connect_error;
      }
    }else
    {
      asprintf(&errmsg, "bad parameter: '%s'", z);
      goto zsvtab_connect_error;
    }
  }

  if(!CSV_FILENAME) {
    asprintf(&errmsg, "No csv filename provided");
    goto zsvtab_connect_error;
  }

  if(!(pNew->parser_opts.stream = fopen(CSV_FILENAME, "rb"))) {
    asprintf(&errmsg, "Unable to open for reading: %s", CSV_FILENAME);
    goto zsvtab_connect_error;
  }

  pNew->zFilename = CSV_FILENAME;
  pNew->opts_used = ZSV_OPTS_USED;
  CSV_FILENAME = ZSV_OPTS_USED = 0; // in use; don't free
  if(zsv_new_with_properties(&pNew->parser_opts, &pNew->custom_prop_handler, pNew->zFilename, pNew->opts_used,
                             &pNew->parser) != zsv_status_ok)
    goto zsvtab_connect_error;

  if((pNew->parser_status = zsv_next_row(pNew->parser)) != zsv_status_row) {
    asprintf(&errmsg, "Could not fetch header row: %s", zsv_parse_status_desc(pNew->parser_status));
    goto zsvtab_connect_error;
  }

  *ppVtab = (sqlite3_vtab*)pNew;

  // generate the CREATE TABLE statement
  sqlite3_str *pStr = sqlite3_str_new(0);
  sqlite3_str_appendf(pStr, "CREATE TABLE x(");

  // for each column, add a spec to CREATE TABLE
  for(size_t i = 0, j = zsv_cell_count(pNew->parser); i < j; i++) {
    struct zsv_cell cell = zsv_get_cell(pNew->parser, i);
    size_t len = cell.len;
    unsigned char *utf8_value = (unsigned char *)zsv_strtrim(cell.str, &len);

    if(!len) {
      if(blank_column_name_count++)
        sqlite3_str_appendf(pStr, "%s\"%s_%u\" TEXT", i > 0 ? "," : "", BLANK_COLUMN_NAME_PREFIX, blank_column_name_count - 1);
      else
        sqlite3_str_appendf(pStr, "%s\"%s\" TEXT", i > 0 ? "," : "", BLANK_COLUMN_NAME_PREFIX);
    } else
      sqlite3_str_appendf(pStr, "%s\"%.*w\" TEXT", i > 0 ? "," : "", len, utf8_value);
    // to do: deal with duplicate column names
  }

  sqlite3_str_appendf(pStr, ")");
  schema = sqlite3_str_finish(pStr);
  if(!schema)
    goto zsvtab_connect_oom;

  // advance cursor to first data row
  pNew->parser_status = zsv_next_row(pNew->parser);
  pNew->rowCount = 1;

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
  sqlite3_free(cur);
  return SQLITE_OK;
}

/*
** Only a full table scan is supported.  So xFilter simply rewinds to
** the beginning.
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
  zsvTable *pTab = (zsvTable*)pVtabCursor->pVtab;

  zsvTable_free(pTab);
  fseek(pTab->parser_opts.stream, 0, SEEK_SET);

  // reload and advance header, then first data row
  if(zsv_new_with_properties(&pTab->parser_opts, &pTab->custom_prop_handler, pTab->zFilename, pTab->opts_used,
                             &pTab->parser) != zsv_status_ok
     || (pTab->parser_status = zsv_next_row(pTab->parser)) != zsv_status_row)
    return SQLITE_ERROR;
  pTab->parser_status = zsv_next_row(pTab->parser);
  pTab->rowCount = 1;
  return SQLITE_OK;
}


/*
** Advance a zsvCursor to its next row of input.
** Set the EOF marker via pTab->parser_status if we reach the end of input.
*/
static int zsvtabNext(sqlite3_vtab_cursor *cur){
  zsvTable *pTab = (zsvTable*)cur->pVtab;
  pTab->parser_status = zsv_next_row(pTab->parser);
  pTab->rowCount++;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int zsvtabEof(sqlite3_vtab_cursor *cur){
  zsvTable *pTab = (zsvTable*)cur->pVtab;
  return pTab->parser_status != zsv_status_row;
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
  zsvTable *pTab = (zsvTable*)cur->pVtab;
  struct zsv_cell c = zsv_get_cell(pTab->parser, i);
  sqlite3_result_text(ctx, (char *)c.str, c.len, SQLITE_STATIC);
  return SQLITE_OK;
}


/*
** Return the rowid for the current row.
*/
static int zsvtabRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  zsvTable *pTab = (zsvTable*)cur->pVtab;
  *pRowid = pTab->rowCount;
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
  rc = sqlite3_create_module(db, "csv", &CsvModule, 0);
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
