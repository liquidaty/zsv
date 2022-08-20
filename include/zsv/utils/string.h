/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ASCII_STRING_H
#define ASCII_STRING_H

#include <stddef.h>

/*
 * zsv_strtolowercase(): convert to lower case. if built with utf8proc, converts unicode points
 *
 * @param s     string to convert
 * @param lenp  pointer to length of input string; will be set to length of output string
 *
 * @returns     newly-allocated string; caller must free()
 */
unsigned char *zsv_strtolowercase(const unsigned char *s, size_t *lenp);

const unsigned char *zsv_strstr(const unsigned char *hay, const unsigned char *needle);

/*
 * zsv_stricmp, zsv_strincmp(): case-insensitive comparison (unicode-compatible if built with utf8proc)
 * zsv_strincmp_ascii: ascii case-insensitive comparison
 *
 * @param   s1     string to convert
 * @param   len1   length of s1
 * @param   s2     string to convert
 * @param   len2   length of s2
 *
 * @returns 0 if the strings are equal, -1 if s1 < s2, else 1
 */
int zsv_stricmp(const unsigned char *s1, const unsigned char *s2);
int zsv_strincmp(const unsigned char *s1, size_t len1, const unsigned char *s2, size_t len2);
int zsv_strincmp_ascii(const unsigned char *s1, size_t len1, const unsigned char *s2, size_t len2);

unsigned char *zsv_strtrim(unsigned char * restrict s, size_t *lenp);

/**
 * zsv_strwhite(): convert consecutive white to single space
 *
 * @param s     string to convert
 * @param len   length of input string
 * @param flags bitfield of ZSV_STRWHITE_FLAG_XXX values
 */
#define ZSV_STRWHITE_FLAG_NO_EMBEDDED_NEWLINE 1
size_t zsv_strwhite(unsigned char *s, size_t len, unsigned int flags);

size_t zsv_strencode(unsigned char *s, size_t n, unsigned char replace);

size_t zsv_strip_trailing_zeros(const char *s, size_t len);

#endif
