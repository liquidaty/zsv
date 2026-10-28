// Unit test for zsv_parse_bytes() chunk boundaries inside a quoted cell:
// when a chunk ends immediately after the opening quote, zsv_finish() must
// keep the final cell's last byte. Exit status is the number of failed checks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zsv.h>

#define ENGINE_COMPAT 255 /* ZSV_MODE_COMPAT */
#define ENGINE_FAST 3     /* ZSV_MODE_DELIM_FAST */

static int failures;
static zsv_parser g_parser;

static struct {
  unsigned char buf[64];
  size_t len;
  int quoted;
  int have;
} g_last;

static void row_handler(void *ctx) {
  (void)ctx;
  size_t n = zsv_cell_count(g_parser);
  if (!n)
    return;
  struct zsv_cell c = zsv_get_cell(g_parser, n - 1);
  g_last.len = c.len < sizeof(g_last.buf) ? c.len : sizeof(g_last.buf);
  if (g_last.len)
    memcpy(g_last.buf, c.str, g_last.len);
  g_last.quoted = c.quoted;
  g_last.have = 1;
}

static void run_case(const char *label, int engine, const char *chunk1, const char *chunk2,
                     const char *expect) {
  struct zsv_opts opts;
  memset(&opts, 0, sizeof opts);
  opts.scan_engine = (unsigned char)engine;
  opts.buffsize = 4096;
  opts.max_row_size = 2048;
  opts.keep_empty_header_rows = 1;

  memset(&g_last, 0, sizeof g_last);
  g_parser = zsv_new(&opts);
  if (!g_parser) {
    failures++;
    fprintf(stderr, "FAIL %s (engine %d): no parser\n", label, engine);
    return;
  }
  zsv_set_row_handler(g_parser, row_handler);
  zsv_set_context(g_parser, g_parser);
  int status_ok = zsv_parse_bytes(g_parser, (const unsigned char *)chunk1, strlen(chunk1)) == zsv_status_ok &&
                  zsv_parse_bytes(g_parser, (const unsigned char *)chunk2, strlen(chunk2)) == zsv_status_ok &&
                  zsv_finish(g_parser) == zsv_status_ok;
  zsv_delete(g_parser);
  g_parser = NULL;

  size_t expect_len = strlen(expect);
  if (!status_ok || !g_last.have || g_last.len != expect_len || memcmp(g_last.buf, expect, expect_len) ||
      g_last.quoted != ZSV_PARSER_QUOTE_CLOSED) {
    failures++;
    fprintf(stderr, "FAIL %s (engine %d): len=%zu quoted=%d value='%.*s', expected len=%zu value='%s' quoted=%d\n",
            label, engine, g_last.len, g_last.quoted, (int)g_last.len, g_last.buf, expect_len, expect,
            ZSV_PARSER_QUOTE_CLOSED);
  }
}

int main(void) {
  static const struct {
    const char *label, *chunk1, *chunk2, *expect;
  } cases[] = {
    {"boundary-after-opening-quote", "h\n\"", "abc", "abc"},
    {"boundary-after-opening-quote-cell2", "x,y\n1,\"", "abc", "abc"},
    {"boundary-inside-cell", "h\n\"a", "bc", "abc"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(cases[i].label, ENGINE_COMPAT, cases[i].chunk1, cases[i].chunk2, cases[i].expect);
    run_case(cases[i].label, ENGINE_FAST, cases[i].chunk1, cases[i].chunk2, cases[i].expect);
  }
  return failures;
}
