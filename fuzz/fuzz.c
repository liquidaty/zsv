#include <unistd.h>
#include "../src/zsv.c"

__AFL_FUZZ_INIT();

int main() {
  __AFL_INIT();
  unsigned char *src = 0;
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    src = realloc(src, len);
    memcpy(src, buf, len);
    zsv_parse_bytes(zsv_new(0), src, len);
  }
}
