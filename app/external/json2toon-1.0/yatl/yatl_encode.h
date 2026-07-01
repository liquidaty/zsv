/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * UTF-8 encoding and TOON escape helpers shared by the parser.
 */
#ifndef __YATL_ENCODE_H__
#define __YATL_ENCODE_H__

#include <stddef.h>

/* Encode code point cp (already validated < 0x110000, not a surrogate) as
 * UTF-8 into out (>= 4 bytes). Returns the number of bytes written (1-4). */
int yatl_utf8_encode(unsigned cp, char *out);

/* Map a single-character TOON escape (the byte after '\\') to its value, or
 * return -1 if it is not one of the spec (§7.1) escapes \" \\ \n \r \t. Other
 * control chars use \uXXXX; unlisted escapes MUST be rejected by the caller. */
int yatl_unescape(unsigned char c);

/* Parse exactly 4 hex digits at s into *out. Returns 0 on success, -1 if any
 * of the 4 bytes is not a hex digit. The caller guarantees 4 readable bytes. */
int yatl_hex4(const char *s, unsigned *out);

/* Return 1 if [s,len) is well-formed UTF-8, else 0. */
int yatl_validate_utf8(const unsigned char *s, size_t len);

#endif
