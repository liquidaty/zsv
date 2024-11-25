#ifndef SQLITE3_CSV_VTAB_ZSV_H
#define SQLITE3_CSV_VTAB_ZSV_H

#include <zsv.h>
#include <zsv/utils/prop.h>

/**
 * when sqlite3 opens a CSV file using ZSV, it needs a way to know
 * what options to open with (such as user-specified delimiter, header offset
 * or span, etc
 *
 * In particular, it needs access to:
 * - zsv options (struct zsv_opts)
 * - custom property handler (struct zsv_prop_handler *)
 * - options used (const char *) [but this can be passed via connection string]
 *
 * Some ways to pass this info are:
 * - Embed it in the text of the URI passed to the module's xConnect function.
 *   This is not practical because we need to pass pointers
 * - Use a single global variable that can hold only one set of data at a time.
 *   This was the old approach, via `zsv_set_default_opts` etc, which has the
 *   usual drawbacks of using a single global variable structure
 * - Use a shared memory structure that can support multiple sets of data
 *   That is the approach implemented here. Data is identified by the related
 *   filename and caller pid
 *
 *   sqlite3_create_module_v2 is passed the shared memory root pointer,
 *   but it's not really needed because there is no way for it to be
 *   dynamic so it always has to point to the single global location
 *
 *   Prior to calling xConnect, the caller should save data for the related
 *   file via `sqlite3_zsv_data_add()`; xConnect then does a lookup to
 *   locate and use the saved data
 */

struct sqlite3_zsv_data;

void sqlite3_zsv_list_delete(struct sqlite3_zsv_data **list);

int sqlite3_zsv_list_add(const char *filename, struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler);

struct sqlite3_zsv_data *sqlite3_csv_vtab_zsv_find(const char *filename);

/**
 * Remove from list. Return 0 on success, non-zero on error
 */
int sqlite3_zsv_list_remove(const char *filename);

#endif
