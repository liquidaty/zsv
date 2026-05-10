
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <zsv.h>

/*
 * test_buffsize_determinism: assert that for any input, the parser's emitted
 * cells (content, length, quoted flag) are byte-identical regardless of the
 * buffsize and max_row_size used.
 */

struct corpus_entry {
  const char *id;
  const unsigned char *data;
  size_t len;
};

static const unsigned char unclosed_simple[] = "\"abc";
static const unsigned char unclosed_embedded[] = "\"a\"\"b\"\"c";
static const unsigned char multi_row_unclosed[] = "a,b\nc,\"def";
static const unsigned char single_quote[] = "\"";
static const unsigned char pair_only[] = "\"\"";

static struct corpus_entry corpus[] = {{"unclosed_simple", unclosed_simple, 4},
                                       {"unclosed_embedded", unclosed_embedded, 8},
                                       {"multi_row_unclosed", multi_row_unclosed, 9},
                                       {"single_quote", single_quote, 1},
                                       {"pair_only", pair_only, 2},
                                       {NULL, NULL, 0}};

struct canonical_out {
  char *buf;
  size_t len;
  size_t cap;
};

static void canonical_append(struct canonical_out *out, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char temp[1024];
  int n = vsnprintf(temp, sizeof(temp), fmt, args);
  va_end(args);

  if (out->len + n + 1 > out->cap) {
    out->cap = (out->len + n + 1) * 2 + 1024;
    out->buf = realloc(out->buf, out->cap);
  }
  memcpy(out->buf + out->len, temp, n);
  out->len += n;
  out->buf[out->len] = '\0';
}

struct wrapper {
  zsv_parser parser;
  struct canonical_out *out;
};

static void row_handler(void *ctx) {
  struct wrapper *w = (struct wrapper *)ctx;
  zsv_parser parser = w->parser;
  struct canonical_out *out = w->out;
  canonical_append(out, "ROW\n");
  size_t n = zsv_cell_count(parser);
  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(parser, i);
    canonical_append(out, "  CELL len=%zu quoted=0x%02x bytes=", c.len, (int)c.quoted);
    for (size_t j = 0; j < c.len; j++)
      canonical_append(out, "%02x ", c.str[j]);
    canonical_append(out, "\n");
  }
}

static char *run_and_capture(const struct corpus_entry *entry, size_t buffsize, size_t max_row_size) {
  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.buffsize = buffsize;
  opts.max_row_size = max_row_size;
  opts.row_handler = row_handler;

  struct canonical_out out = {0};
  zsv_parser parser = zsv_new(&opts);
  struct wrapper w = {parser, &out};
  zsv_set_context(parser, &w);

  zsv_parse_bytes(parser, entry->data, entry->len);
  zsv_finish(parser);
  zsv_delete(parser);

  return out.buf;
}

int main() {
  int failed = 0;
  for (int i = 0; corpus[i].id; i++) {
    const struct corpus_entry *entry = &corpus[i];
    printf("Testing ID: %s\n", entry->id);

    char *ref = NULL;
    size_t ref_buffsize = 0;

    // Sweep buffsize
    // Starting from 4096 because ZSV_MIN_SCANNER_BUFFSIZE is 4096
    size_t sweep[] = {4096, 4097, 4098, 8192, 16384, 0};
    for (int j = 0; sweep[j]; j++) {
      size_t buffsize = sweep[j];
      size_t max_row_size = 2048; // Fixed max_row_size

      char *actual = run_and_capture(entry, buffsize, max_row_size);
      if (!ref) {
        ref = actual;
        ref_buffsize = buffsize;
      } else {
        if (strcmp(ref, actual) != 0) {
          fprintf(stderr, "DIVERGENCE: input=%s\n", entry->id);
          fprintf(stderr, "  reference (buffsize=%zu):\n%s\n", ref_buffsize, ref);
          fprintf(stderr, "  actual    (buffsize=%zu):\n%s\n", buffsize, actual);
          failed = 1;
        }
        free(actual);
      }
      if (failed)
        break;
    }
    free(ref);
    if (failed)
      break;
  }

  if (!failed)
    printf("All tests passed!\n");
  return failed;
}
