/*
 * Regression PoC: FAST SIMD scanner heap-buffer-overflow on non-standard CSV.
 *
 * Bug
 * ---
 * The fast SIMD engine's prefix-XOR algorithm toggles inside_quote on every
 * `"`, diverging from the COMPAT engine (which per RFC 4180 only treats `"`
 * as a state toggle when it appears at a cell boundary). On non-standard
 * input — a quote in the middle of an unquoted cell that fills the scanner
 * buffer with no row terminator and ends on `"` — the fast engine left
 * scanner state as { cell_start=0, scanned_length=buff.size,
 * quoted=UNCLOSED, last='"' }. zsv_finish's EOF fix-up then drove cell_dl
 * into the slow memmove(s+1, s, quote_close_position) branch with
 * quote_close_position == buff.size, overrunning the heap allocation by
 * one byte. AddressSanitizer aborts at zsv.c (cell_dl call inside
 * zsv_finish) with heap-buffer-overflow.
 *
 * Fix
 * ---
 * (1) End-of-scan safety gate: only carry UNCLOSED out of zsv_scan_delim_fast
 *     if buff[cell_start] is actually `"`. Restores COMPAT's invariant.
 * (2) Per-block non-standard detection: opening quote whose previous byte is
 *     not a delim/CR/LF/`"` triggers an abort with zsv_status_nonstandard_csv.
 *
 * This PoC is built under -fsanitize=address,undefined. Pre-fix it aborts
 * with an ASan heap-buffer-overflow. Post-fix it exits 0 and the parser
 * returns zsv_status_nonstandard_csv (with detection enabled).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zsv.h>

static volatile unsigned int g_sink;

static void row_handler(void *ctx) {
  zsv_parser p = (zsv_parser)ctx;
  size_t n = zsv_cell_count(p);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(p, i);
    for (size_t j = 0; j < c.len; j++)
      g_sink += c.str[j];
  }
}

/* Build an input that drives the FAST SIMD scanner into the OOB-triggering
 * state on the unfixed code:
 *   - Buffer-sized payload (so the cell fills the buffer with no row end).
 *   - Cell starts with non-quote content (`abc`) so the first `"` is mid-cell.
 *     COMPAT would treat it as a literal; prefix-XOR toggles state.
 *   - Trailing region of alternating `d"` pattern with the final byte = `"`,
 *     so prefix-XOR ends with odd parity (inside_quote=1) AND scanner->last
 *     becomes `"` after parse, which is what selects the buggy memmove path
 *     in zsv_finish.
 */
static void build_input(unsigned char *buf, size_t N) {
  memset(buf, 'a', N);
  buf[0] = 'a';
  buf[1] = 'b';
  buf[2] = 'c';
  buf[3] = '"'; /* mid-cell quote — non-standard */
  size_t i = 4;
  /* fill the rest with alternating `d` and `"` so quotes have odd parity */
  for (; i < N - 1; i += 2) {
    buf[i] = 'd';
    buf[i + 1] = '"';
  }
  if (i < N)
    buf[i] = '"'; /* ensure last byte is `"` (scanner->last == '"') */
  buf[N - 1] = '"';
}

static int run_one(unsigned char *input, size_t N, unsigned char *user_buff, unsigned char engine,
                   enum zsv_status *out_status) {
  struct zsv_opts opts;
  memset(&opts, 0, sizeof opts);
  opts.max_columns = 256;
  opts.max_row_size = (unsigned)(N / 2);
  opts.buffsize = N;
  opts.buff = user_buff;
  opts.scan_engine = engine;
  opts.keep_empty_header_rows = 1;

  zsv_parser p = zsv_new(&opts);
  if (!p)
    return 2;
  zsv_set_row_handler(p, row_handler);
  zsv_set_context(p, p);

  enum zsv_status st = zsv_parse_bytes(p, input, N);
  enum zsv_status st_fin = zsv_finish(p);
  zsv_delete(p);
  *out_status = (st != zsv_status_ok) ? st : st_fin;
  return 0;
}

int main(void) {
  const size_t N = 4096; /* ZSV_MIN_SCANNER_BUFFSIZE */

  unsigned char *input = malloc(N);
  unsigned char *user_buff = malloc(N);
  if (!input || !user_buff)
    return 2;

  build_input(input, N);

  /* 1) Fast SIMD on non-standard input must not OOB. With detection on
   * (the default), the parser should return zsv_status_nonstandard_csv. */
  enum zsv_status st_fast = zsv_status_ok;
  if (run_one(input, N, user_buff, 3 /* ZSV_MODE_DELIM_FAST */, &st_fast) != 0) {
    fprintf(stderr, "FAIL: fast-engine setup failed\n");
    free(input);
    free(user_buff);
    return 1;
  }

#ifdef ZSV_NO_NONSTANDARD_CHECK
  /* Detection compiled out: success is "no OOB" (ASan would have caught it).
   * status may be ok; just confirm the parser didn't error out unexpectedly. */
  if (st_fast != zsv_status_ok) {
    fprintf(stderr, "FAIL: fast engine without detection should return ok; got %d\n", st_fast);
    free(input);
    free(user_buff);
    return 1;
  }
#else
  if (st_fast != zsv_status_nonstandard_csv) {
    fprintf(stderr,
            "FAIL: fast engine should detect non-standard CSV and return "
            "zsv_status_nonstandard_csv (%d); got %d\n",
            (int)zsv_status_nonstandard_csv, (int)st_fast);
    free(input);
    free(user_buff);
    return 1;
  }
#endif

  /* 2) COMPAT engine on the same input must continue to parse without OOB
   * (different output is fine; we only care about safety). */
  enum zsv_status st_compat = zsv_status_ok;
  if (run_one(input, N, user_buff, 255 /* ZSV_MODE_COMPAT */, &st_compat) != 0) {
    fprintf(stderr, "FAIL: compat-engine setup failed\n");
    free(input);
    free(user_buff);
    return 1;
  }
  if (st_compat != zsv_status_ok) {
    fprintf(stderr, "FAIL: compat engine should accept the input; got status %d\n", (int)st_compat);
    free(input);
    free(user_buff);
    return 1;
  }

  free(input);
  free(user_buff);

  printf("PASS: fast-engine non-standard OOB regression\n");
  return 0;
}
