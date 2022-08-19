/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include <zsv/utils/compiler.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/string.h>

#ifndef NO_UTF8PROC
#include <utf8proc.h>

static utf8proc_int32_t utf8proc_tolower1(utf8proc_int32_t codepoint, void *data) {
  (void)data;
  return utf8proc_tolower(codepoint);
}

static unsigned char *utf8proc_tolower_str(const unsigned char *str, size_t *len) {
  utf8proc_uint8_t *output;
  utf8proc_option_t options;
  /* options = UTF8PROC_COMPOSE | UTF8PROC_COMPAT
  | UTF8PROC_CASEFOLD | UTF8PROC_IGNORE | UTF8PROC_STRIPMARK | UTF8PROC_STRIPCC | UTF8PROC_STRIPNA;
  */
  memset(&options, 0, sizeof(options));

  // utf8proc_map_custom allocates new mem
  options = UTF8PROC_STRIPNA;
  *len = (size_t) utf8proc_map_custom((const utf8proc_uint8_t *)str, (utf8proc_ssize_t)*len, &output,
                                      options, utf8proc_tolower1, NULL);
  return (unsigned char *)output;
}
#endif // ndef NO_UTF8PROC

// zsv_strtolowercase(): to do: utf8 support
unsigned char *zsv_strtolowercase(const unsigned char *s, size_t *lenp) {
#ifndef NO_UTF8PROC
  size_t len_orig = *lenp;
  unsigned char *new_s = utf8proc_tolower_str(s, lenp);
  if(!new_s && len_orig) { //
    unsigned char *tmp_s = malloc(len_orig + 1);
    if(tmp_s) {
      fprintf(stderr, "Warning: malformed UTF8 '%.*s'\n", (int)len_orig, s);
      memcpy(tmp_s, s, len_orig);
      tmp_s[len_orig] = '\0';
      *lenp = zsv_strencode(tmp_s, len_orig, '?');
      new_s = utf8proc_tolower_str(tmp_s, lenp);
      free(tmp_s);
    }
  }
#else // ndef NO_UTF8PROC
  unsigned char *new_s = malloc((*lenp + 1) * sizeof(*new_s));
  if(new_s) {
    for(size_t i = 0, j = *lenp; i < j; i++)
      new_s[i] = tolower(s[i]);
    new_s[*lenp] = '\0';
  }
#endif // ndef NO_UTF8PROC
  return new_s;
}

// zsv_strstr(): strstr
const unsigned char *zsv_strstr(const unsigned char *hay, const unsigned char *needle) {
  return (const unsigned char *)strstr(
                                       (const char *)hay,
                                       (const char *)needle);
}

/*
 * zsv_stricmp, zsv_strincmp(): case-insensitive comparison
 *
 * @param s1     string to convert
 * @param len1   length of s1
 * @param s2     string to convert
 * @param len2   length of s2
 */
int zsv_stricmp(const unsigned char *s1, const unsigned char *s2) {
  return zsv_strincmp(s1, strlen((const char *)s1), s2, strlen((const char *)s2));
}

int zsv_strincmp(const unsigned char *s1, size_t len1, const unsigned char *s2, size_t len2) {
#ifndef NO_UTF8PROC
  unsigned char *lc1 = zsv_strtolowercase(s1, &len1);
  unsigned char *lc2 = zsv_strtolowercase(s2, &len2);
  int result;
  if(VERY_UNLIKELY(!lc1 || !lc2))
    fprintf(stderr, "Out of memory!\n"), result = -2;
  else
    result = strcmp((char *)lc1, (char *)lc2);
  free(lc1);
  free(lc2);
  return result;
#else
  while(len1) {
    if(!*s1)
      return *s2 == 0 ? 0 : -1;
    if(!*s2)
      return 1;
    int c1 = tolower(*s1);
    int c2 = tolower(*s2);
    if(c1 == c2) {
      s1++, s2++, len1--;
    } else
      return c1 < c2 ? -1 : 1;
  }
  return 0;
#endif
}

// zsv_trim(): trim leading and trailing white space. to do: utf8 support
unsigned char *zsv_strtrim(char unsigned * restrict s, size_t *lenp) {
  if(UNLIKELY(s == NULL))
    return s;
  while(isspace(*s) && *lenp)
    s++, (*lenp)--;
  while(*lenp && isspace(s[(*lenp)-1]))
    (*lenp)--;
  return s;
}

/**
 * zsv_strwhite(): convert consecutive white to single space
 *
 * @param s     string to convert
 * @param len   length of input string
 * @param flags bitfield of ZSV_STRWHITE_FLAG_XXX values
 */
size_t zsv_strwhite(unsigned char *s, size_t len, unsigned int flags) {
  int this_is_space, last_was_space = 0;
  size_t new_len = 0;
  char replacement = ' ';
  int clen;

  for(size_t i = 0; i < len; i += clen) {
#ifndef NO_UTF8PROC
    clen = ZSV_UTF8_CHARLEN(s[i]);
    this_is_space = 0;
    if(UNLIKELY(clen < 1 || i + clen > len)) { // bad UTF8. replace w '?'
      clen = 1;
      s[i] = '?';
    } else if(UNLIKELY(clen > 1)) { // multi-byte UTF8
      utf8proc_int32_t codepoint;
      utf8proc_ssize_t bytes_read = utf8proc_iterate(s + i, clen, &codepoint);
      if(UNLIKELY(bytes_read < 1)) { // error! but this could only happen if ZSV_UTF8_CHARLEN was wrong
        clen = 1;
        s[i] = '?';
      } else {
        utf8proc_category_t category = utf8proc_category(codepoint);
        switch(category) { // Unicode space categories
        case UTF8PROC_CATEGORY_ZL: // line separator
        case UTF8PROC_CATEGORY_ZP: // paragraph separator
          this_is_space = 1;
          s[i] = (flags & ZSV_STRWHITE_FLAG_NO_EMBEDDED_NEWLINE ? ' ' : '\n');
          break;
        case UTF8PROC_CATEGORY_ZS: // regular space
          this_is_space = 1;
          s[i] = ' ';
          break;
        default:
          break;
        }
      }
    } else { // regular ascii, clen == 1
      this_is_space = isspace(s[i]);
    }
#else // no UTF8PROC, assume clen = 1
    {
      clen = 1;
      this_is_space = isspace(s[i]);
    }
#endif // ndef NO_UTF8PROC

    if(this_is_space) {
      if(UNLIKELY((s[i] == '\n' || s[i] == '\r')) && !(flags & ZSV_STRWHITE_FLAG_NO_EMBEDDED_NEWLINE))
        replacement = '\n';
      else if(!last_was_space)
        replacement = ' ';
      last_was_space = 1;
    } else {
      // current position is not a space
      if(last_was_space)
        s[new_len++] = replacement;
      for(int j = 0; j < clen; j++)
        s[new_len++] = s[i + j];
      last_was_space = 0;
    }
  }
  return new_len;
}

// zsv_strencode(): force UTF8 encoding
// replace any non-conforming utf8 with the specified char, or
// remove from the string (and shorten the string) if replace = 0.
// returns the length of the valid string
size_t zsv_strencode(unsigned char *s, size_t n, unsigned char replace) {
  size_t new_len = 0;
  int clen;
  for(size_t i2 = 0; i2 < n; i2 += (size_t)clen) {
    clen = ZSV_UTF8_CHARLEN(s[i2]);
    if(LIKELY(clen == 1))
      s[new_len++] = s[i2];
    else if(UNLIKELY(clen < 0) || UNLIKELY(i2 + clen >= n)) {
      if(replace)
        s[new_len++] = replace;
      clen = 1;
    } else { /* might be valid multi-byte utf8; check */
      unsigned char valid_n;
      for(valid_n = 1; valid_n < clen; valid_n++)
        if(!ZSV_UTF8_SUBSEQUENT_CHAR_OK(s[i2 + valid_n]))
          break;
      if(valid_n == clen) { /* valid_n utf8; copy it */
        memmove(s + new_len, s + i2, clen);
        new_len += clen;
      } else { /* invalid; valid_n smaller than expected */
        memset(s + new_len, replace, valid_n);
        new_len += valid_n;
        clen = valid_n;
      }
    }
  }
  return new_len; // new length
}

size_t zsv_strip_trailing_zeros(const char *s, size_t len) {
  if(len && memchr(s, '.', len)) {
    while(len && s[len-1] == '0')
      len--;
    if(len && s[len-1] == '.')
      len--;
  }
  return len;
}
