#ifndef ZSV_OVERWRITE_H
#define ZSV_OVERWRITE_H

/**
 * The easiest way to enable overwrite support is to use zsv_overwrite_auto()
 * which, given an input located at /path/to/my-data.csv, will assume an overwrite source
 * located at /path/to/.zsv/data/my-data.csv/overwrites.sqlite3
 * in a table named 'overwrites'
 *
 * zsv_overwrite_auto() returns:
 * - zsv_status_done if a valid overwrite file was found
 * - zsv_status_no_more_input if no overwrite file was found
 * - a different status code on error
 *
 * This function is used in app/cli.c
 */
enum zsv_status zsv_overwrite_auto(struct zsv_opts *opts, const char *csv_filename);

/**
 * As an alternative to zsv_overwrite_auto(), you can specify your own
 * source from which to fetch overwrite data.
 * The specified source can be a sqlite3 file or a CSV file
 *
 * This approach is used by app/echo.c
 */
struct zsv_overwrite_opts {
  /* src may be either:
   *
   * 1. sqlite3 file source:
   *    sqlite3://<filename>[?sql=<query>]",
   *
   * e.g. sqlite3://overwrites.db?sql=select row, column, value from overwrites order by row, column",
   *
   * or
   *
   * 2. CSV file source
   *   /path/to/file.csv",
   * where the CSV columns are row,col,val (in that order, with a header row),
   * and rows already-sorted by row and column",
   */
  const char *src;
};

void *zsv_overwrite_context_new(struct zsv_overwrite_opts *);
enum zsv_status zsv_overwrite_next(void *h, struct zsv_overwrite_data *odata);
enum zsv_status zsv_overwrite_open(void *h);
enum zsv_status zsv_overwrite_context_delete(void *h);

#endif
