/*
 * Regression test for `zsv_next_row()` with `max_rows`.
 *
 * The bug dropped the final permitted row in pull mode, so the parser could
 * return `zsv_status_max_rows_read` before handing back the row it had just
 * parsed. This test checks the exact row count returned by the pull API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zsv.h>

static int run_case(const char *label, size_t max_rows, size_t want_rows) {
  FILE *f = tmpfile();
  if (!f) {
    perror("tmpfile");
    return 1;
  }

  fputs("h1,h2\n1,2\n3,4\n", f);
  rewind(f);

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.stream = f;
  opts.max_rows = max_rows;

  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    fclose(f);
    fprintf(stderr, "FAIL [%s]: zsv_new failed\n", label);
    return 1;
  }

  size_t got_rows = 0;
  enum zsv_status st;
  while ((st = zsv_next_row(parser)) == zsv_status_row)
    got_rows++;

  zsv_delete(parser);
  fclose(f);

  if (got_rows != want_rows || st != zsv_status_done) {
    fprintf(stderr, "FAIL [%s]: rows=%zu status=%d, want rows=%zu status=%d\n", label, got_rows, st, want_rows,
            zsv_status_done);
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;
  failures += run_case("limit-1", 1, 1);
  failures += run_case("limit-2", 2, 2);
  return failures ? 1 : 0;
}
