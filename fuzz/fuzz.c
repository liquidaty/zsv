/* AFL stubs (used when compiling without afl-clang-fast) */
#ifndef __AFL_COMPILER
#define __AFL_HAVE_MANUAL_CONTROL
#define __AFL_INIT()                                                                                                   \
  do {                                                                                                                 \
  } while (0)
static unsigned char __afl_buf[1 << 20];
static int __afl_len;
#define __AFL_FUZZ_INIT()
#define __AFL_LOOP(n) ((__afl_len = (int)fread(__afl_buf, 1, sizeof(__afl_buf), stdin)) > 0)
#define __AFL_FUZZ_TESTCASE_BUF __afl_buf
#define __AFL_FUZZ_TESTCASE_LEN __afl_len
#endif /* __AFL_COMPILER */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include "../src/zsv.c"

__AFL_FUZZ_INIT();

/* Volatile sink — prevents the compiler from eliding cell reads. */
static volatile size_t g_sink;

static void row_handler(void *ctx) {
  zsv_parser parser = (zsv_parser)ctx;
  size_t n = zsv_cell_count(parser);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(parser, i);
    if (!c.str)
      continue; /* guard: zsv may return NULL str on truncated rows */
    for (size_t j = 0; j < c.len; j++)
      g_sink += c.str[j];
    g_sink += c.len;
    g_sink += (size_t)(unsigned char)c.quoted;
  }
  g_sink += zsv_row_is_blank(parser) ? 1 : 0;
}

static int dummy_errprintf(void *ctx, const char *fmt, ...) {
  (void)ctx;
  (void)fmt;
  return 0;
}

static void fuzz_push(const unsigned char *data, size_t size, unsigned char scan_engine, char no_quotes,
                      char delimiter) {
  struct zsv_opts opts = {0};
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.scan_engine = scan_engine;
  opts.no_quotes = no_quotes;
  opts.keep_empty_header_rows = 1;
  opts.errprintf = dummy_errprintf;
  if (delimiter != 0 && delimiter != '\n' && delimiter != '\r' && delimiter != '"')
    opts.delimiter = delimiter;

  zsv_parser parser = zsv_new(&opts);
  if (!parser)
    return;

  zsv_set_row_handler(parser, row_handler);
  zsv_set_context(parser, parser);
  zsv_parse_bytes(parser, data, size);
  zsv_finish(parser);
  zsv_delete(parser);
}

struct membuf {
  const unsigned char *data;
  size_t size;
  size_t pos;
};

static size_t membuf_read(void *dst, size_t n, size_t elem_size, void *stream) {
  struct membuf *mb = (struct membuf *)stream;
  size_t want = n * elem_size;
  size_t avail = mb->size - mb->pos;
  size_t give = want < avail ? want : avail;
  if (give) {
    memcpy(dst, mb->data + mb->pos, give);
    mb->pos += give;
  }
  return elem_size > 0 ? give / elem_size : 0;
}

static void fuzz_pull(const unsigned char *data, size_t size, char no_quotes) {
  struct membuf mb;
  mb.data = data;
  mb.size = size;
  mb.pos = 0;

  struct zsv_opts opts = {0};
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.no_quotes = no_quotes;
  opts.keep_empty_header_rows = 1;
  opts.errprintf = dummy_errprintf;
  opts.stream = (FILE *)&mb;
  opts.read = membuf_read;

  zsv_parser parser = zsv_new(&opts);
  if (parser) {
    /* zsv_next_row() installs its own internal row handler, so we must NOT
     * use zsv_set_row_handler() here — it would be silently overwritten.
     * Instead, call row_handler() manually after each successful pull. */
    while (zsv_next_row(parser) == zsv_status_row)
      row_handler(parser);
    zsv_finish(parser);
    zsv_delete(parser);
  }
}

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
#endif
  const unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
  while (__AFL_LOOP(10000)) {
    const int len = __AFL_FUZZ_TESTCASE_LEN;
    if (len < 2)
      continue;

    const unsigned char mode = buf[0] & 0x7;
    const unsigned char *payload = buf + 1;
    const size_t payload_size = (size_t)(len - 1);
    const char delim = payload_size > 0 ? (char)payload[0] : (char)';';

#ifndef __AFL_COMPILER
    // Log the test case details for debugging purposes (only when not compiling with AFL)
    fprintf(stderr, "MODE: 0x%02x\n", mode);
    if (isprint(delim)) {
      fprintf(stderr, "DELIM: '%c' (0x%02x)\n", delim, (unsigned char)delim);
    } else {
      fprintf(stderr, "DELIM: [NON-PRINTABLE] (0x%02x)\n", (unsigned char)delim);
    }
    fprintf(stderr, "PAYLOAD SIZE: %zu\n", payload_size);
#endif

    switch (mode) {
    case 0:
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 0, 0);
      break;
    case 1: /* fast SIMD */
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM_FAST, 0, 0);
      break;
    case 2: /* compat */
      fuzz_push(payload, payload_size, ZSV_MODE_COMPAT, 0, 0);
      break;
    case 3: /* alternate delimiter */
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 0, delim);
      break;
    case 4: /* no-quotes */
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 1, 0);
      break;
    case 5: /* pull */
      fuzz_pull(payload, payload_size, 0);
      break;
    case 6: /* pull, no-quotes */
      fuzz_pull(payload, payload_size, 1);
      break;
    case 7: /* compat + alternate delimiter */
      fuzz_push(payload, payload_size, ZSV_MODE_COMPAT, 0, delim);
      break;
    }
  }
  return 0;
}
