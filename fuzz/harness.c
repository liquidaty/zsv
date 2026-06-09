#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include "../src/zsv.c"

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

static void fuzz_push_chunked(const unsigned char *data, size_t size, unsigned char scan_engine, size_t chunk_size) {
  struct zsv_opts opts = {0};
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.scan_engine = scan_engine;
  opts.keep_empty_header_rows = 1;
  opts.errprintf = dummy_errprintf;

  zsv_parser parser = zsv_new(&opts);
  if (!parser)
    return;

  zsv_set_row_handler(parser, row_handler);
  zsv_set_context(parser, parser);

  size_t off = 0;
  while (off < size) {
    size_t chunk = size - off;
    if (chunk > chunk_size)
      chunk = chunk_size;
    zsv_parse_bytes(parser, data + off, chunk);
    off += chunk;
  }
  zsv_finish(parser);
  zsv_delete(parser);
}

static void fuzz_fixed(const unsigned char *data, size_t size) {
  if (size < 8)
    return;

  size_t offsets[4];
  size_t prev = 0;
  for (int i = 0; i < 4; i++) {
    size_t step = (data[i] & 0x3f) + 1;
    offsets[i] = prev + step;
    prev = offsets[i];
  }

  struct zsv_opts opts = {0};
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.keep_empty_header_rows = 1;
  opts.errprintf = dummy_errprintf;

  zsv_parser parser = zsv_new(&opts);
  if (!parser)
    return;

  zsv_set_row_handler(parser, row_handler);
  zsv_set_context(parser, parser);

  if (zsv_set_fixed_offsets(parser, 4, offsets) == zsv_status_ok) {
    zsv_parse_bytes(parser, data + 4, size - 4);
    zsv_finish(parser);
  }
  zsv_delete(parser);
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

void run_payload(const unsigned char *data, const size_t size) {
  if (size < 2)
    return;

  const unsigned char ctrl = data[0];
  const unsigned char mode = ctrl & 0x7;
  const int chunked = (ctrl & 0x8) != 0; /* bit 3: feed in 17-byte chunks */
  const unsigned char *payload = data + 1;
  const size_t payload_size = size - 1;
  const char delim = payload_size > 0 ? (char)payload[0] : (char)';';

#ifdef REPRO
  fprintf(stderr, "MODE: 0x%02x CHUNKED: %d\n", mode, chunked);
  if (isprint(delim)) {
    fprintf(stderr, "DELIM: '%c' (0x%02x)\n", delim, (unsigned char)delim);
  } else {
    fprintf(stderr, "DELIM: [NON-PRINTABLE] (0x%02x)\n", (unsigned char)delim);
  }
  fprintf(stderr, "PAYLOAD SIZE: %zu\n", payload_size);
#endif

  switch (mode) {
  case 0: /* default engine */
    if (chunked)
      fuzz_push_chunked(payload, payload_size, ZSV_MODE_DELIM, 17);
    else
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 0, 0);
    break;
  case 1: /* fast SIMD engine */
    if (chunked)
      fuzz_push_chunked(payload, payload_size, ZSV_MODE_DELIM_FAST, 17);
    else
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM_FAST, 0, 0);
    break;
  case 2: /* compat engine */
    if (chunked)
      fuzz_push_chunked(payload, payload_size, ZSV_MODE_COMPAT, 17);
    else
      fuzz_push(payload, payload_size, ZSV_MODE_COMPAT, 0, 0);
    break;
  case 3: /* alternate delimiter */
    fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 0, delim);
    break;
  case 4: /* no-quotes */
    if (chunked)
      fuzz_push_chunked(payload, payload_size, ZSV_MODE_DELIM, 17);
    else
      fuzz_push(payload, payload_size, ZSV_MODE_DELIM, 1, 0);
    break;
  case 5: /* fixed-width */
    fuzz_fixed(payload, payload_size);
    break;
  case 6: /* pull */
    fuzz_pull(payload, payload_size, 0);
    break;
  case 7: /* pull, no-quotes */
    fuzz_pull(payload, payload_size, 1);
    break;
  }
}

// AFL++ | LibFuzzer | repro

#ifdef AFL_HARNESS

// AFL++ Persistent Mode
// https://github.com/AFLplusplus/AFLplusplus/blob/stable/instrumentation/README.persistent_mode.md

__AFL_FUZZ_INIT();

int main(void) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
#endif
  const unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
  while (__AFL_LOOP(10000)) {
    const size_t len = __AFL_FUZZ_TESTCASE_LEN;
    run_payload(buf, len);
  }
  return 0;
}

#elif defined(LIBFUZZER_HARNESS)

// AFL++ driver for LibFuzzer harness
// https://github.com/AFLplusplus/AFLplusplus/blob/stable/utils/aflpp_driver/README.md#aflpp_driver

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  run_payload(data, size);
  return 0;
}

#else /* repro */

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <crash_file>\n", argv[0]);
    return 1;
  }

  const char *filename = argv[1];
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    perror(filename);
    return 1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    perror(filename);
    fclose(fp);
    return 1;
  }

  const long size = ftell(fp);
  if (size == -1L) {
    perror(filename);
    fclose(fp);
    return 1;
  }

  if (size < 3) {
    fprintf(stderr, "file too short\n");
    fclose(fp);
    return 1;
  }

  unsigned char *buf = malloc(size);
  if (!buf) {
    perror("malloc");
    fclose(fp);
    return 1;
  }

  rewind(fp);
  const size_t bytes_read = fread(buf, 1, (size_t)size, fp);
  fclose(fp);

  if (bytes_read < (size_t)size) {
    fprintf(stderr, "warning: short read (%zu/%ld bytes)\n", bytes_read, size);
  }

  run_payload(buf, bytes_read);

  free(buf);
  return 0;
}

#endif /* LIBFUZZER_HARNESS */
