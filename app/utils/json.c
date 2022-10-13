/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

static inline char is_json_esc_char(unsigned char c) {
  return (c == '"' || c == '\\' || c < 32);
}

static void str2hex(unsigned char *to, const unsigned char *p, size_t len) {
  static const char *hex = "0123456789abcdef";

  for (; len--; p++) {
    *to++ = hex[p[0] >> 4];
    *to++ = hex[p[0] & 0x0f];
  }
}

static inline unsigned json_esc_char(unsigned char c, char replace[]) {
  unsigned replacelen;
  char hex = 0;
  switch(c) {
  case '"':
    replace[1] = '"';
    break;
  case '\\':
    replace[1] = '\\';
    break;
  case '\b':
    replace[1] = 'b';
    break;
  case '\f':
    replace[1] = 'f';
    break;
  case '\n':
    replace[1] = 'n';
    break;
  case '\r':
    replace[1] = 'r';
    break;
  case '\t':
    replace[1] = 't';
    break;
  default:
    hex=1;
  }

  replace[0] = '\\';
  if(hex) {
    // unicode-hex: but 2/3 are not always zeroes...
    replace[1] = 'u';
    replace[2] = '0';
    replace[3] = '0';
    str2hex((unsigned char *)replace+4, &c, 1);
    replacelen = 6;
    replace[6] = '\0';
  } else {
    replacelen = 2;
    replace[2] = '\0';
  }
  return replacelen;
}

static unsigned json_escaped_str_len(const unsigned char *s, size_t len) {
  unsigned count = 0;
  for(size_t i = 0; i < len; i++, count++) {
    switch (s[i]) {
    case '"':
    case '\\':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
      count++;
      break;
    default:
      if (s[i] < 31)
        count += 6;
      break;
    }
  }
  return count + 2; // + 2 for surrounding quotation marks
}

unsigned char *zsv_json_from_str_n(const unsigned char *s, size_t len) {
  size_t new_len = json_escaped_str_len(s, len);
  unsigned char *new_s = calloc(new_len + 2, sizeof(*new_s));
  if(new_s) {
    new_s[0] = new_s[new_len-1] = (unsigned char) '"';
    if(new_len == len + 2)
      memcpy(new_s + 1, s, len);
    else {
      char replace[8];
      for(size_t i = 0, j = 1; i < len && j < new_len - 1; i++) {
        if(!is_json_esc_char(s[i]))
          new_s[j++] = s[i];
        else {
          size_t rlen = json_esc_char(s[i], replace);
          memcpy(new_s + j, replace, rlen);
          j += rlen;
        }
      }
    }
  }
  return new_s;
}

unsigned char *zsv_json_from_str(const unsigned char *s) {
  if(!s)
    return (unsigned char *)strdup("null");
  return zsv_json_from_str_n(s, strlen((const char *)s));
}
