/*
 * zsv_strencode(): standalone file to allow zsv utilities that use this
 * to be used on a standalone basis without the zsv parser
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/utf8.h>
#include <zsv/utils/compiler.h>

/* Single scan loop shared by zsv_strencode (encode in place) and
 * zsv_strencode_validate (read-only). validate_only is a compile-time
 * constant at each entry point, so each instantiation constant-folds; with
 * validate_only set the loop provably never writes through s. Returns the
 * encoded length (with validate_only, the length removal would leave);
 * *malformed_count, if given, receives the number of malformed sequences. */
static inline size_t zsv_strencode_aux(unsigned char *s, size_t n, unsigned char replace,
                                       int (*malformed_handler)(void *, const unsigned char *s, size_t n,
                                                                size_t offset),
                                       void *handler_ctx, char validate_only, size_t *malformed_count) {
  size_t new_len = 0, bad = 0;
  int clen;
  for (size_t i2 = 0; i2 < n; i2 += (size_t)clen) {
    clen = ZSV_UTF8_CHARLEN(s[i2]);
    if (LIKELY(clen == 1)) {
      if (!validate_only)
        s[new_len] = s[i2];
      new_len++;
    } else if (UNLIKELY(clen < 0) || UNLIKELY(i2 + clen > n)) {
      bad++;
      if (malformed_handler)
        malformed_handler(handler_ctx, s, n, new_len);
      if (!validate_only && replace)
        s[new_len++] = replace;
      clen = 1;
    } else { /* might be valid multi-byte utf8; check */
      unsigned char valid_n;
      for (valid_n = 1; valid_n < clen; valid_n++)
        if (!ZSV_UTF8_SUBSEQUENT_CHAR_OK(s[i2 + valid_n]))
          break;
      if (valid_n == clen) { /* valid_n utf8; copy it */
        if (!validate_only)
          memmove(s + new_len, s + i2, clen);
        new_len += clen;
      } else { /* invalid; valid_n smaller than expected */
        bad++;
        if (malformed_handler)
          malformed_handler(handler_ctx, s, n, new_len);
        if (!validate_only && replace) {
          memset(s + new_len, replace, valid_n);
          new_len += valid_n;
        }
        clen = valid_n;
      }
    }
  }
  if (malformed_count)
    *malformed_count = bad;
  return new_len;
}

/**
 * Ensure valid UTF8 encoding by, if needed, replacing malformed bytes
 */
ZSV_EXPORT
size_t zsv_strencode(unsigned char *s, size_t n, unsigned char replace,
                     int (*malformed_handler)(void *, const unsigned char *s, size_t n, size_t offset),
                     void *handler_ctx) {
  return zsv_strencode_aux(s, n, replace, malformed_handler, handler_ctx, 0, NULL);
}

/**
 * Scan for malformed UTF8 without modifying the input
 */
ZSV_EXPORT
size_t zsv_strencode_validate(const unsigned char *s, size_t n,
                              int (*malformed_handler)(void *, const unsigned char *s, size_t n, size_t offset),
                              void *handler_ctx) {
  size_t bad;
  /* validate_only is compile-time 1: the core never writes through s */
  zsv_strencode_aux((unsigned char *)s, n, 0, malformed_handler, handler_ctx, 1, &bad);
  return bad;
}
