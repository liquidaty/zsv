/*
 * Standalone PoC reader for the integer-underflow vulnerability class.
 *
 * Reads a single payload from a file on the command line, parses it
 * through the fast scan engine, and prints {len, ptr} for each cell. To
 * surface info-leak / wild-pointer regressions, the row handler also
 * dereferences each cell byte; any read of an invalid pointer will be
 * caught by the AddressSanitizer build (see Makefile target
 * test-vuln-asan-repro).
 *
 * Usage: repro <payload-file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zsv.h"

static volatile unsigned int g_sink;

static void row_handler(void *ctx) {
  zsv_parser parser = (zsv_parser)ctx;
  size_t n = zsv_cell_count(parser);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(parser, i);
    printf("cell %zu: len=%zu quoted=%d ptr=%p\n", i, c.len, c.quoted, (void *)c.str);
    fflush(stdout);
    /* Dereference every byte the parser claims is part of the cell. With
     * ASan instrumentation, an out-of-bounds read here is reported. */
    for (size_t j = 0; j < c.len; j++)
      g_sink += c.str[j];
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <payload-file>\n", argv[0]);
    return 1;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror("fopen");
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *payload = malloc(size > 0 ? (size_t)size : 1);
  if (!payload) {
    fclose(f);
    return 1;
  }
  if (size > 0)
    fread(payload, 1, (size_t)size, f);
  fclose(f);

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.scan_engine = 3; /* ZSV_MODE_DELIM_FAST */

  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    free(payload);
    return 1;
  }
  zsv_set_row_handler(parser, row_handler);
  zsv_set_context(parser, parser);

  zsv_parse_bytes(parser, payload, (size_t)size);
  zsv_finish(parser);
  zsv_delete(parser);
  free(payload);
  return 0;
}
