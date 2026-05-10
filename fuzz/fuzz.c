#include <unistd.h>
#include "../src/zsv.c"

__AFL_FUZZ_INIT();

int main() {
  __AFL_INIT();
  unsigned char *src = 0;
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
#define ZSV_PARSER_SIMD 3
  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    src = realloc(src, len);
    memcpy(src, buf, len);
    struct zsv_opts opts = { 0 };
#ifdef FUZZ_TEST_SIMD
    opts.scan_engine = ZSV_PARSER_SIMD;
#endif
    opts.buffsize = 4096;
    opts.max_row_size = 2048;
    zsv_parse_bytes(zsv_new(&opts), src, len);
  }
}
