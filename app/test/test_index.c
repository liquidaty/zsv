/*
 * Unit test of the row index (app/utils/index.c) as sheet builds it: from a CSV writer's
 * on_row/on_delete hooks (edit.c, transformation.c), and from the parser's row handler
 * (sheet/index.c). Seeking to data row r leaves the stream where data row r + 1 begins,
 * including for a header-only file, a header that writes no bytes, a file past the first
 * stored offset, and a source with a BOM and CRLF row ends
 */
#include <stdio.h>
#include <string.h>
#include <zsv.h>
#include <zsv/utils/writer.h>
#include "../utils/index.h"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

// rows of the file past the first stored offset: one more than the index stores rows between
#define TEST_INDEX_LONG_ROWS (ZSV_INDEX_ROW_N + 1)

struct writer_hooks {
  struct zsv_index *ix;
  zsv_csv_writer w;
  char failed;
};

static void writer_on_row(void *p) {
  struct writer_hooks *h = p;
  if (zsv_index_add_row(h->ix, zsv_writer_cum_bytes_written(h->w)) != zsv_index_status_ok)
    h->failed = 1;
}

static void writer_on_delete(void *p) {
  struct writer_hooks *h = p;
  if (zsv_index_add_last_row(h->ix, zsv_writer_cum_bytes_written(h->w)) != zsv_index_status_ok)
    h->failed = 1;
  zsv_index_commit_rows(h->ix);
}

// Write one single-cell row per value to f (rewound afterwards), indexing them in ix
static int write_indexed(FILE *f, struct zsv_index *ix, const char *const *values, size_t n) {
  struct writer_hooks h = {.ix = ix};
  h.w = zsv_writer_new(&(struct zsv_csv_writer_options){
    .stream = f,
    .on_row = writer_on_row,
    .on_row_ctx = &h,
    .on_delete = writer_on_delete,
    .on_delete_ctx = &h,
  });
  CHECK(h.w);
  for (size_t i = 0; i < n; i++)
    CHECK(zsv_writer_cell(h.w, 1, (const unsigned char *)values[i], strlen(values[i]), 0) == zsv_writer_status_ok);
  CHECK(zsv_writer_delete(h.w) == zsv_writer_status_ok);
  CHECK(!h.failed && !fflush(f));
  rewind(f);
  return 0;
}

struct parser_hooks {
  struct zsv_index *ix;
  zsv_parser parser;
  char failed;
};

static void parser_on_row(void *p) {
  struct parser_hooks *h = p;
  if (zsv_index_add_row(h->ix, zsv_cum_scanned_length(h->parser)) != zsv_index_status_ok)
    h->failed = 1;
}

// Index f (rewound afterwards) as sheet/index.c does
static int parse_indexed(FILE *f, struct zsv_index *ix) {
  struct parser_hooks h = {.ix = ix};
  struct zsv_opts opts = {.stream = f, .ctx = &h, .row_handler = parser_on_row};
  CHECK((h.parser = zsv_new(&opts)));
  enum zsv_status zst;
  while ((zst = zsv_parse_more(h.parser)) == zsv_status_ok)
    ;
  zsv_finish(h.parser);
  zsv_delete(h.parser);
  CHECK(zst == zsv_status_no_more_input && !h.failed);
  zsv_index_commit_rows(ix);
  rewind(f);
  return 0;
}

// Seek f to data row `row` and check what follows: a row whose first cell is `expect`, or,
// if expect is NULL, no row
static int check_seek(FILE *f, const struct zsv_index *ix, uint64_t row, const char *expect) {
  struct zsv_opts opts = {.stream = f, .keep_empty_header_rows = 1};
  CHECK(zsv_index_seek_row(ix, &opts, row) == zsv_index_status_ok);
  zsv_parser parser = zsv_new(&opts);
  CHECK(parser);
  enum zsv_status zst = zsv_next_row(parser);
  int ok = expect
             ? zst == zsv_status_row && zsv_cell_count(parser) > 0 && zsv_get_cell(parser, 0).len == strlen(expect) &&
                 !memcmp(zsv_get_cell(parser, 0).str, expect, strlen(expect))
             : zst != zsv_status_row;
  zsv_delete(parser);
  if (!ok)
    fprintf(stderr, "seek to row %llu: expected %s\n", (unsigned long long)row, expect ? expect : "no row");
  CHECK(ok);
  rewind(f);
  return 0;
}

// Run body, a case that fills and checks a fresh temp file f and index ix, then free both
#define WITH_FILE(f, ix, body)                                                                                         \
  do {                                                                                                                 \
    FILE *f = tmpfile();                                                                                               \
    struct zsv_index *ix = zsv_index_new();                                                                            \
    CHECK(f != NULL && ix != NULL);                                                                                    \
    int failed_ = (body);                                                                                              \
    zsv_index_delete(ix);                                                                                              \
    fclose(f);                                                                                                         \
    if (failed_)                                                                                                       \
      return failed_;                                                                                                  \
  } while (0)

static int header_only(FILE *f, struct zsv_index *ix) {
  const char *const values[] = {"h"};
  CHECK(!write_indexed(f, ix, values, 1));
  CHECK(ix->row_count == 0);
  return check_seek(f, ix, 0, NULL);
}

static int empty_header(FILE *f, struct zsv_index *ix) {
  const char *const values[] = {"", "x", "y"}; // the header writes no bytes before its newline
  CHECK(!write_indexed(f, ix, values, 3));
  CHECK(ix->row_count == 2);
  CHECK(!check_seek(f, ix, 0, "x"));
  CHECK(!check_seek(f, ix, 1, "y"));
  return check_seek(f, ix, 2, NULL);
}

static int long_file(FILE *f, struct zsv_index *ix) {
  static char bufs[TEST_INDEX_LONG_ROWS + 1][16];
  const char *values[TEST_INDEX_LONG_ROWS + 1];
  for (size_t i = 0; i <= TEST_INDEX_LONG_ROWS; i++) {
    snprintf(bufs[i], sizeof(bufs[i]), "r%zu", i); // r0 is the header
    values[i] = bufs[i];
  }
  CHECK(!write_indexed(f, ix, values, TEST_INDEX_LONG_ROWS + 1));
  CHECK(ix->row_count == TEST_INDEX_LONG_ROWS);
  CHECK(!check_seek(f, ix, 0, "r1"));
  CHECK(!check_seek(f, ix, ZSV_INDEX_ROW_N - 1, bufs[ZSV_INDEX_ROW_N]));
  CHECK(!check_seek(f, ix, ZSV_INDEX_ROW_N, bufs[TEST_INDEX_LONG_ROWS]));
  return check_seek(f, ix, TEST_INDEX_LONG_ROWS, NULL); // the last row's end, which on_delete records
}

static int parsed_bom_crlf(FILE *f, struct zsv_index *ix) {
  CHECK(fputs("\xef\xbb\xbfh\r\nx\r\ny\r\n", f) >= 0 && !fflush(f));
  rewind(f);
  CHECK(!parse_indexed(f, ix));
  CHECK(ix->row_count == 2);
  CHECK(!check_seek(f, ix, 0, "x"));
  CHECK(!check_seek(f, ix, 1, "y"));
  return check_seek(f, ix, 2, NULL);
}

int main(void) {
  WITH_FILE(f, ix, header_only(f, ix));
  WITH_FILE(f, ix, empty_header(f, ix));
  WITH_FILE(f, ix, long_file(f, ix));
  WITH_FILE(f, ix, parsed_bom_crlf(f, ix));
  return 0;
}
