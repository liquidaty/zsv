#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool utf8_is_valid(const uint8_t *s, size_t len);

/////
static inline bool is_cont(uint8_t b) {
  // Continuation byte: 10xxxxxx
  return (b & 0xC0) == 0x80;
}

bool utf8_is_valid(const uint8_t *s, size_t len) {
  size_t i = 0;

  while (i < len) {
    uint8_t c = s[i];

    // ASCII fast path
    if (c < 0x80) {
      i++;
      continue;
    }

    // Reject continuation bytes as leading bytes, and C0/C1 overlongs
    if (c < 0xC2) {
      return false;
    }

    // 2-byte sequence: 110xxxxx 10xxxxxx
    if (c < 0xE0) {
      if (i + 1 >= len)
        return false;
      uint8_t c1 = s[i + 1];
      if (!is_cont(c1))
        return false;
      i += 2;
      continue;
    }

    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    if (c < 0xF0) {
      if (i + 2 >= len)
        return false;
      uint8_t c1 = s[i + 1];
      uint8_t c2 = s[i + 2];

      // Special rules to avoid overlongs and surrogates:
      //   E0 A0–BF 80–BF
      //   E1–EC 80–BF 80–BF
      //   ED 80–9F 80–BF (avoid D800–DFFF)
      //   EE–EF 80–BF 80–BF
      if (!is_cont(c2))
        return false;

      switch (c) {
      case 0xE0:
        if (c1 < 0xA0 || c1 > 0xBF)
          return false;
        break;
      case 0xED:
        if (c1 < 0x80 || c1 > 0x9F)
          return false;
        break;
      default:
        if (!is_cont(c1))
          return false;
        break;
      }

      i += 3;
      continue;
    }

    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    // Valid range is up to U+10FFFF:
    //   F0 90–BF 80–BF 80–BF
    //   F1–F3 80–BF 80–BF 80–BF
    //   F4 80–8F 80–BF 80–BF
    if (c < 0xF5) {
      if (i + 3 >= len)
        return false;
      uint8_t c1 = s[i + 1];
      uint8_t c2 = s[i + 2];
      uint8_t c3 = s[i + 3];

      if (!is_cont(c2) || !is_cont(c3))
        return false;

      switch (c) {
      case 0xF0:
        // Avoid overlongs: codepoints < 0x10000
        if (c1 < 0x90 || c1 > 0xBF)
          return false;
        break;
      case 0xF4:
        // Avoid values > U+10FFFF
        if (c1 < 0x80 || c1 > 0x8F)
          return false;
        break;
      default:
        if (!is_cont(c1))
          return false;
        break;
      }

      i += 4;
      continue;
    }

    // 0xF5–0xFF are invalid in UTF-8
    return false;
  }

  return true;
}
