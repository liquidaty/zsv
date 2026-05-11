/*
 * Regression PoC: zsv_finish() -> cell_dl() heap-buffer-overflow.
 *
 * Bug
 * ---
 * When the input ends inside an unclosed quoted cell, zsv_finish()'s EOF
 * fix-up sets `quote_close_position = pending` (one past the last cell
 * byte) and, when the previous chunk's last byte was not a `"`, also
 * does `scanner->scanned_length++`. The subsequent cell_dl() call then
 * believes the quoted cell is one byte longer than it actually is. With
 * the EMBEDDED flag set on scanner->quoted (compat scanner propagates it
 * for `""` pairs inside the cell), cell_dl()'s embedded-quote scrub does
 * an in-place memmove plus a per-byte scan that reads/writes one byte
 * past `scanner->scanned_length`. With a tightly-sized scanner buffer
 * (input length == buff.size), that overruns the heap allocation and
 * AddressSanitizer aborts with a heap-buffer-overflow at zsv.c
 * inside zsv_finish.
 *
 * This PoC is built with -fsanitize=address,undefined; on the unfixed
 * code it terminates with a SUMMARY: AddressSanitizer report. After the
 * fix in src/zsv.c (zsv_finish EOF fix-up no longer over-extends the
 * cell), it exits 0.
 *
 * Repro shape (length == buff.size == ZSV_MIN_SCANNER_BUFFSIZE == 4096):
 *   `"` + repeated (`a` `a` `"` `"`) + tail of `a`s
 * The leading `"` opens a quoted cell. The repeated `""` pairs set
 * EMBEDDED. There is no closing quote and the trailing byte is `a`, not
 * `"`, which is exactly the path that increments scanned_length.
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
    /* Touch every byte the parser claims is part of the cell. With ASan,
     * this surfaces any wild pointer or len that exceeds valid input. */
    for (size_t j = 0; j < c.len; j++)
      g_sink += c.str[j];
  }
}

int main(void) {
  const size_t N = 4096; /* ZSV_MIN_SCANNER_BUFFSIZE */

  unsigned char *input = calloc(1, N);
  unsigned char *user_buff = malloc(N);
  if (!input || !user_buff)
    return 2;

  input[0] = '"';
  size_t i = 1;
  while (i + 4 <= N - 1) { /* leave at least 1 byte for non-quote tail */
    input[i++] = 'a';
    input[i++] = 'a';
    input[i++] = '"';
    input[i++] = '"';
  }
  while (i < N)
    input[i++] = 'a';

  struct zsv_opts opts;
  memset(&opts, 0, sizeof opts);
  opts.max_columns = 256;
  opts.max_row_size = (unsigned)(N / 2); /* so 2*max_row_size == buffsize */
  opts.buffsize = N;
  opts.buff = user_buff;
  opts.scan_engine = 255;             /* force compat engine: propagates EMBEDDED */
  opts.keep_empty_header_rows = 1;

  zsv_parser p = zsv_new(&opts);
  if (!p) {
    free(input);
    free(user_buff);
    return 2;
  }
  zsv_set_row_handler(p, row_handler);
  zsv_set_context(p, p);

  zsv_parse_bytes(p, input, N);
  zsv_finish(p);
  zsv_delete(p);

  free(input);
  free(user_buff);

  printf("PASS: finish/cell_dl OOB regression\n");
  return 0;
}
