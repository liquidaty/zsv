/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 */
#include "yatl_encode.h"

int yatl_utf8_encode(unsigned cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int yatl_unescape(unsigned char c) {
    switch (c) {
        case '"':  return 0x22;
        case '\\': return 0x5C;
        case 'n':  return 0x0A;
        case 'r':  return 0x0D;
        case 't':  return 0x09;
        default:   return -1;
    }
}

static int hexval(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int yatl_hex4(const char *s, unsigned *out) {
    unsigned v = 0;
    int i, d;
    for (i = 0; i < 4; i++) {
        d = hexval((unsigned char)s[i]);
        if (d < 0)
            return -1;
        v = (v << 4) | (unsigned)d;
    }
    *out = v;
    return 0;
}

/* Standard UTF-8 well-formedness check (rejects overlong forms, surrogates and
 * code points above U+10FFFF). */
int yatl_validate_utf8(const unsigned char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = s[i];
        if (c < 0x80) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
                return 0;
            if ((c & 0x1E) == 0)                 /* overlong */
                return 0;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= len ||
                (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80)
                return 0;
            if (c == 0xE0 && (s[i + 1] & 0x20) == 0)     /* overlong */
                return 0;
            if (c == 0xED && (s[i + 1] & 0x20) != 0)     /* surrogate */
                return 0;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= len ||
                (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 ||
                (s[i + 3] & 0xC0) != 0x80)
                return 0;
            if (c == 0xF0 && (s[i + 1] & 0x30) == 0)     /* overlong */
                return 0;
            if (c > 0xF4 || (c == 0xF4 && (s[i + 1] & 0x30) != 0))  /* > U+10FFFF */
                return 0;
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}
