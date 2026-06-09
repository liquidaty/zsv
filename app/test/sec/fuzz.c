/*
 * In-process random/mutation fuzzer for the fast CSV scan engine.
 *
 * Not coverage-guided: this is a portable smoke-test fuzzer that runs
 * anywhere clang+ASan works (no libFuzzer / AFL toolchain required).
 * Loads seeds from a directory, mutates copies of them via simple
 * byte-level operations (flip / set / insert / delete / favored-byte),
 * and feeds each mutated input to zsv_parse_bytes() in chunks. With the
 * sanitizer build (Makefile target test-vuln-asan-fuzz), any heap-OOB
 * or undefined-behavior triggered by a mutation aborts the process.
 *
 * Usage: fuzz <seeds-dir> [iterations]
 *   - iterations defaults to 1000000.
 *   - each seed file must be <= 4096 bytes.
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "zsv.h"

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

static void run_one(const unsigned char *data, size_t len) {
  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.max_columns = 256;
  opts.max_row_size = 4096;
  opts.scan_engine = 3;
  zsv_parser p = zsv_new(&opts);
  if (!p)
    return;
  zsv_set_row_handler(p, row_handler);
  zsv_set_context(p, p);
  /* Feed in 17-byte chunks to exercise refill paths */
  size_t off = 0;
  while (off < len) {
    size_t chunk = len - off;
    if (chunk > 17)
      chunk = 17;
    zsv_parse_bytes(p, (unsigned char *)data + off, chunk);
    off += chunk;
  }
  zsv_finish(p);
  zsv_delete(p);
}

static void mutate(unsigned char *buf, size_t *plen, size_t cap) {
  if (cap == 0)
    return;
  int op = rand() % 5;
  switch (op) {
  case 0: { /* bit flip */
    if (*plen == 0)
      return;
    size_t i = (size_t)rand() % *plen;
    buf[i] ^= 1u << (rand() & 7);
    break;
  }
  case 1: { /* byte set */
    if (*plen == 0)
      return;
    size_t i = (size_t)rand() % *plen;
    buf[i] = (unsigned char)rand();
    break;
  }
  case 2: { /* byte insert */
    if (*plen >= cap)
      return;
    size_t i = (size_t)rand() % (*plen + 1);
    memmove(buf + i + 1, buf + i, *plen - i);
    buf[i] = (unsigned char)rand();
    (*plen)++;
    break;
  }
  case 3: { /* byte delete */
    if (*plen == 0)
      return;
    size_t i = (size_t)rand() % *plen;
    memmove(buf + i, buf + i + 1, *plen - i - 1);
    (*plen)--;
    break;
  }
  case 4: { /* favored bytes */
    if (*plen == 0)
      return;
    static const unsigned char favs[] = {'"', ',', '\n', '\r', 0, 0xff};
    size_t i = (size_t)rand() % *plen;
    buf[i] = favs[rand() % (int)sizeof(favs)];
    break;
  }
  }
}

static unsigned char **g_seeds;
static size_t *g_seed_lens;
static size_t g_nseeds;

static void load_seeds(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) {
    fprintf(stderr, "opendir %s: %s\n", dir, strerror(errno));
    exit(1);
  }
  struct dirent *e;
  size_t cap = 16;
  g_seeds = malloc(cap * sizeof *g_seeds);
  g_seed_lens = malloc(cap * sizeof *g_seed_lens);
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    FILE *f = fopen(path, "rb");
    if (!f)
      continue;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 4096) {
      fclose(f);
      continue;
    }
    if (g_nseeds == cap) {
      cap *= 2;
      g_seeds = realloc(g_seeds, cap * sizeof *g_seeds);
      g_seed_lens = realloc(g_seed_lens, cap * sizeof *g_seed_lens);
    }
    g_seeds[g_nseeds] = malloc(sz > 0 ? (size_t)sz : 1);
    g_seed_lens[g_nseeds] = (size_t)sz;
    if (sz > 0)
      fread(g_seeds[g_nseeds], 1, (size_t)sz, f);
    g_nseeds++;
    fclose(f);
  }
  closedir(d);
  fprintf(stderr, "[fuzz] loaded %zu seeds from %s\n", g_nseeds, dir);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <seeds-dir> [iterations]\n", argv[0]);
    return 2;
  }
  load_seeds(argv[1]);
  if (g_nseeds == 0) {
    fprintf(stderr, "[fuzz] no seeds loaded\n");
    return 1;
  }
  unsigned long iters = argc >= 3 ? strtoul(argv[2], NULL, 10) : 1000000UL;
  srand((unsigned)time(NULL));

  unsigned char buf[4096];
  for (unsigned long i = 0; i < iters; i++) {
    size_t s = (size_t)rand() % g_nseeds;
    size_t len = g_seed_lens[s];
    if (len > sizeof buf)
      len = sizeof buf;
    memcpy(buf, g_seeds[s], len);
    int nm = 1 + rand() % 8;
    for (int j = 0; j < nm; j++)
      mutate(buf, &len, sizeof buf);
    run_one(buf, len);
  }
  fprintf(stderr, "[fuzz] done %lu iters, no crash\n", iters);
  return 0;
}
