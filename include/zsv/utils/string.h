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

unsigned char *zsv_strtolowercase(const unsigned char *s, size_t *lenp);

const unsigned char *zsv_strstr(const unsigned char *hay, const unsigned char *needle);

int zsv_stricmp(const unsigned char *s1, const unsigned char *s2);

int zsv_strincmp(const unsigned char *s1, size_t len1, const unsigned char *s2, size_t len2);

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

#endif
