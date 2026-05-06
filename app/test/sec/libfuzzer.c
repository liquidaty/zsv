/*
 * libFuzzer entry point for zsv_parse_bytes() on the fast scan engine.
 *
 * Compiled and driven by libFuzzer / OSS-Fuzz. The harness:
 *   - Constructs a parser, set up to use the fast scan engine.
 *   - Feeds the test input in fixed 17-byte chunks to exercise the
 *     buffer-refill path (the historical bug class lives there).
 *   - In the row callback, dereferences every byte the parser claims
 *     is part of each cell, so an out-of-bounds read or read of an
 *     uninitialized byte will be reported by AddressSanitizer or
 *     MemorySanitizer respectively.
 *
 * Seed corpus lives in app/test/sec/poc/.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zsv.h"

static volatile unsigned int g_sink;

static void row_handler(void *ctx) {
  zsv_parser p = (zsv_parser)ctx;
  size_t n = zsv_cell_count(p);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(p, i);
    /* Touch every byte so OOB / uninit reads are flagged by sanitizers. */
    for (size_t j = 0; j < c.len; j++)
      g_sink += c.str[j];
  }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  /* Cap input size to keep per-iteration cost bounded. */
  if (size > (1u << 20))
    return 0;

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.scan_engine = 3; /* ZSV_MODE_DELIM_FAST */
  opts.max_columns = 256;
  opts.max_row_size = 4096;

  zsv_parser parser = zsv_new(&opts);
  if (!parser)
    return 0;
  zsv_set_row_handler(parser, row_handler);
  zsv_set_context(parser, parser);

  size_t off = 0;
  while (off < size) {
    size_t chunk = size - off;
    if (chunk > 17)
      chunk = 17;
    zsv_parse_bytes(parser, (unsigned char *)data + off, chunk);
    off += chunk;
  }
  zsv_finish(parser);
  zsv_delete(parser);
  return 0;
}
