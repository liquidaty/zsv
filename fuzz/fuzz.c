#include <string.h>
#include <unistd.h>
#include "../src/zsv.c"

/**
 * FUZZ_PARSER selects which parser engine to exercise:
 *   0   = library default (compat unless built with -DZSV_DEFAULT_PARSER_FAST)
 *   3   = fast (branchless SIMD) — ZSV_MODE_DELIM_FAST
 *   255 = force compat/standard — ZSV_MODE_DELIM
 */
#ifndef FUZZ_PARSER
#define FUZZ_PARSER 0
#endif

__AFL_FUZZ_INIT();

int main() {
  __AFL_INIT();
  unsigned char *src = 0;
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    src = realloc(src, len);
    memcpy(src, buf, len);
    struct zsv_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.scan_engine = FUZZ_PARSER;
    zsv_parser p = zsv_new(&opts);
    zsv_parse_bytes(p, src, len);
    zsv_finish(p);
    zsv_delete(p);
  }
}
