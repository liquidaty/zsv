/*
 * Regression test for integer underflow vulnerabilities in the fast CSV scanner.
 *
 * Covers:
 *   1. Original PoC payloads from issue.md (5-byte malformed inputs that
 *      previously caused stack-buffer-underflow / native segfault).
 *   2. The historical 10-byte synthetic that produced SIZE_MAX cell lengths.
 *   3. A bare single-quote payload that hits the n -= 2 underflow site
 *      in zsv_finish() -> cell_dl().
 *
 * Each case asserts: parser does not crash, and no cell has a length so
 * large that it can only have come from an underflow (>65536).
 *
 * The single-quote case adds a content check: when fed a bare `"`, the
 * parser must emit one cell of length 1 whose byte equals `"`. The previous
 * (incorrect) behavior was to emit a length-0 cell with a pointer past the
 * end of the input bytes — a masked underflow that leaked uninitialized
 * heap content to consumers that read c.str[0] without checking c.len.
 *
 * Exit code 0 = pass; non-zero = vulnerability present.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

#include <zsv.h>

static jmp_buf crash_jmp;
static volatile int received_signal = 0;

static const char *g_case_name;
static int g_failed = 0;

/* Optional content-check state. */
static int g_check_first_cell_byte = 0;
static unsigned char g_expected_first_byte;
static int g_first_cell_seen = 0;
static int g_keep_empty_header_rows = 0;

static void signal_handler(int sig) {
  received_signal = sig;
  longjmp(crash_jmp, 1);
}

static void check_row(void *ctx) {
  zsv_parser parser = (zsv_parser)ctx;
  size_t n = zsv_cell_count(parser);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(parser, i);
    if (c.len > 65536) {
      printf("FAIL [%s]: cell %zu has length %zu (underflow symptom)\n", g_case_name, i, c.len);
      g_failed = 1;
      return;
    }
    if (g_check_first_cell_byte && !g_first_cell_seen) {
      g_first_cell_seen = 1;
      if (c.len != 1 || c.str == NULL || c.str[0] != g_expected_first_byte) {
        printf("FAIL [%s]: first cell expected len=1 byte=0x%02x, got len=%zu byte=0x%02x\n", g_case_name,
               g_expected_first_byte, c.len, (c.len && c.str) ? c.str[0] : 0);
        g_failed = 1;
        return;
      }
    }
  }
}

static int run_case(const char *name, const unsigned char *payload, size_t payload_len) {
  g_case_name = name;
  g_failed = 0;
  g_first_cell_seen = 0;

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.scan_engine = 3; /* ZSV_MODE_DELIM_FAST */
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.keep_empty_header_rows = g_keep_empty_header_rows;

  zsv_parser parser = zsv_new(&opts);
  if (!parser)
    return 1;
  zsv_set_row_handler(parser, check_row);
  zsv_set_context(parser, parser);

  signal(SIGSEGV, signal_handler);
  signal(SIGBUS, signal_handler);
  signal(SIGABRT, signal_handler);

  if (setjmp(crash_jmp) != 0) {
    printf("FAIL [%s]: received signal %d (crash)\n", name, received_signal);
    zsv_delete(parser);
    return 1;
  }

  if (payload_len)
    zsv_parse_bytes(parser, (unsigned char *)payload, payload_len);
  zsv_finish(parser);
  zsv_delete(parser);
  return g_failed;
}

int main(void) {
  printf("=== ZSV integer-underflow regression test ===\n");

  /* PoC payloads from the original issue report. 5 bytes each. */
  static const unsigned char poc_stack_ptr_invalid[] = {0x69, 0x64, 0x00, 0x2c, 0x04};
  static const unsigned char poc_native_segv[] = {0x29, 0x69, 0x64, 0x2c, 0x04};

  /* Historical synthetic from the original committed test. 10 bytes. */
  static const unsigned char historical[] = {0x32, 0x02, 0x61, 0x2c, 0x22, 0x2c, 0x3c, 0x22, 0x0a, 0x22};

  /* Single bare quote: triggers the n < 2 quote-strip underflow site in
   * cell_dl(). After the fix, must emit one cell with len=1, byte='"'. */
  static const unsigned char single_quote[] = {'"'};

  static const unsigned char empty[] = {0};
  const char *normal = "name,age\nAlice,25\nBob,30\n";

  int failures = 0;
  failures += run_case("poc_stack_ptr_invalid", poc_stack_ptr_invalid, sizeof poc_stack_ptr_invalid);
  failures += run_case("poc_native_segv", poc_native_segv, sizeof poc_native_segv);
  failures += run_case("historical_size_max", historical, sizeof historical);

  /* Need keep_empty_header_rows=1 here: otherwise a single-cell row whose
   * cell length is 0 (the buggy "masked" output) would be silently dropped
   * by the empty-header-skip logic, hiding the regression. */
  g_check_first_cell_byte = 1;
  g_expected_first_byte = '"';
  g_keep_empty_header_rows = 1;
  failures += run_case("single_quote", single_quote, sizeof single_quote);
  g_check_first_cell_byte = 0;
  g_keep_empty_header_rows = 0;

  failures += run_case("empty_input", empty, 0);
  failures += run_case("normal_csv", (const unsigned char *)normal, strlen(normal));

  if (failures) {
    printf("FAIL: %d case(s) failed\n", failures);
    return 1;
  }
  printf("PASS: all underflow regression cases\n");
  return 0;
}
