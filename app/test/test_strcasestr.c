// Unit test for zsv_strfold() / zsv_strcasestr_folded(): case-insensitive UTF-8 substring search.
// Exit status is the number of failed checks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zsv/utils/string.h>

static int failures;

#define CHECK(cond, ...)                                                                                               \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      failures++;                                                                                                      \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                             \
      fprintf(stderr, __VA_ARGS__);                                                                                    \
      fputc('\n', stderr);                                                                                             \
    }                                                                                                                  \
  } while (0)

// Fold `needle` and report whether `hay` (of hay_len bytes) contains it. With an ASCII
// needle both kernels are run and must agree; `flagsp` receives the fold flags.
static int contains(const char *hay, size_t hay_len, const char *needle, unsigned *flagsp) {
  size_t n;
  unsigned flags;
  int32_t *fold = zsv_strfold((const unsigned char *)needle, strlen(needle), &n, &flags);
  CHECK(fold != NULL, "zsv_strfold(\"%s\") returned NULL", needle);
  if (!fold)
    return -1;
  int general = zsv_strcasestr_folded((const unsigned char *)hay, hay_len, fold, n, 0);
  int rc = general;
  if (flags & ZSV_STRFOLD_ASCII) {
    rc = zsv_strcasestr_folded((const unsigned char *)hay, hay_len, fold, n, 1);
    CHECK(rc == general, "kernels disagree for needle \"%s\": ascii=%d general=%d", needle, rc, general);
  }
  free(fold);
  if (flagsp)
    *flagsp = flags;
  return rc;
}

#define IN(hay, needle)                                                                                                \
  CHECK(contains(hay, strlen(hay), needle, NULL) == 1, "\"%s\" should contain \"%s\"", hay, needle)
#define NOT_IN(hay, needle)                                                                                            \
  CHECK(contains(hay, strlen(hay), needle, NULL) == 0, "\"%s\" should not contain \"%s\"", hay, needle)

// CPU seconds for `reps` scans of hay[0, len) for `needle`
static double scan_secs(const char *hay, size_t len, const char *needle, int reps) {
  size_t n;
  unsigned flags;
  int32_t *fold = zsv_strfold((const unsigned char *)needle, strlen(needle), &n, &flags);
  CHECK(fold != NULL, "zsv_strfold(\"%s\") returned NULL", needle);
  clock_t start = clock();
  for (int r = 0; fold && r < reps; r++)
    zsv_strcasestr_folded((const unsigned char *)hay, len, fold, n, (flags & ZSV_STRFOLD_ASCII) != 0);
  clock_t end = clock();
  free(fold);
  return (double)(end - start) / CLOCKS_PER_SEC;
}

static void check_flags(const char *needle, unsigned expected) {
  unsigned flags;
  contains("", 0, needle, &flags);
  CHECK(flags == expected, "flags for \"%s\": expected %u, got %u", needle, expected, flags);
}

int main(void) {
  // ASCII
  IN("xABC", "abc");
  IN("abc", "ABC");
  IN("xxbc", "bc");    // needle at the very end
  IN("aaab", "aab");   // overlapping candidates
  IN("a", "");         // empty needle is found everywhere
  NOT_IN("ab", "abc"); // needle longer than the haystack
  NOT_IN("", "a");
  NOT_IN("Abd", "abc");
  IN("1,xabc", "ABC");
  IN("Row #,Value", "row #");
  check_flags("123", ZSV_STRFOLD_ASCII | ZSV_STRFOLD_EXACT);
  check_flags("1a", ZSV_STRFOLD_ASCII);
  check_flags("Ab", ZSV_STRFOLD_ASCII);
  check_flags("\xC3\xB6", 0);                  // ö
  check_flags("a\xC3", ZSV_STRFOLD_MALFORMED); // malformed needle forces the general kernel

  // Unicode simple case mapping
  IN("D\xC3\x96MITZ", "d\xC3\xB6mitz");                   // DÖMITZ contains dömitz
  IN("D\xC3\xB6mitzow", "D\xC3\x96MITZ");                 // Dömitzow contains DÖMITZ
  IN("\xCE\x91\xCE\x98\xCE\x89\xCE\x9D\xCE\x91",          // ΑΘΉΝΑ
     "\xCE\xB1\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1");         // αθήνα
  IN("\xD0\x9C\xD0\x9E\xD0\xA1\xD0\x9A\xD0\x92\xD0\x90",  // МОСКВА
     "\xD0\xBC\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0"); // москва
  NOT_IN("D\xC3\x96MITZ", "domitz");                      // ö is not o

  // U+0130 and U+212A are the only non-ASCII code points whose lowercase is ASCII
  {
    unsigned flags;
    CHECK(contains("ISTANBUL", 8, "\xC4\xB0stanbul", &flags) == 1, "Istanbul via U+0130 needle");
    CHECK(flags == ZSV_STRFOLD_ASCII, "U+0130 needle folds to ASCII, got flags %u", flags);
  }
  IN("istanbul", "\xC4\xB0STANBUL");
  IN("\xC4\xB0stanbul", "istanbul"); // İ in the haystack, ASCII needle
  IN("\342\204\252elvin", "kelvin"); // KELVIN SIGN (octal: a hex escape would swallow the e)
  IN("x\xE2\x84\xAA", "K");
  NOT_IN("\342\204\253elvin", "kelvin"); // ANGSTROM SIGN folds to å, not k
  NOT_IN("i", "\xC4\xB1");               // dotless ı stays distinct from i
  NOT_IN("\xC4\xB1", "i");

  // Malformed input: a bad byte matches only itself and never derails the scan
  IN("\xC3(abc", "abc");
  NOT_IN("\xC3\xA9", "\xC3"); // é is a valid sequence, not a stray C3
  IN("x\xC3", "\xC3");
  IN("ab\xC4", "ab");
  NOT_IN("\xC4", "i"); // truncated İ

  // NUL is transparent (the filter matches a whole NUL-separated row span)
  CHECK(contains("ab\0CD", 5, "cd", NULL) == 1, "match across an embedded NUL");
  CHECK(contains("ab\0CD", 5, "bc", NULL) == 0, "no match spanning the NUL");

  // Simple mapping only: no full case folding, no final-sigma equivalence
  NOT_IN("\xC3\x9F", "ss");       // ß
  IN("\xC3\x9F", "\xE1\xBA\x9E"); // ẞ lowercases to ß
  NOT_IN("\xCF\x82", "\xCF\x83"); // ς vs σ

  // Large haystacks: the ASCII kernel must stay linear when the other-case form of the first
  // needle byte is dense in the tail
  {
    size_t big = 1u << 20;
    char *hay = malloc(big + 1);
    CHECK(hay != NULL, "malloc");
    if (hay) {
      memset(hay, 'U', big);
      hay[big] = '\0';
      NOT_IN(hay, "usx");
      hay[0] = 'e';
      NOT_IN(hay, "ERROR");
      memcpy(hay + big - 3, "uSx", 3);
      IN(hay, "USX");                           // at the very end, after the tail of candidates
      memcpy(hay + big - 6, "K\xC3\x96mIT", 6); // ends in Ö, which needs the general kernel
      IN(hay, "k\xC3\xB6mit");
      // Linearity gate independent of machine speed: sixteen 64 KB scans do the work of one
      // 1 MB scan when the cost is linear, while a quadratic scan makes the 1 MB scan ~16x slower
      memset(hay, 'U', big);
      double small = scan_secs(hay, big / 16, "usx", 16), large = scan_secs(hay, big, "usx", 1);
      CHECK(large < 4 * small + 0.01, "1 MB scan %.4fs vs 16 x 64 KB scans %.4fs: no longer linear", large, small);
      free(hay);
    }
  }

  if (failures)
    fprintf(stderr, "%d check(s) failed\n", failures);
  else
    printf("test_strcasestr: all checks passed\n");
  return failures;
}
