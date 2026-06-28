/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 *
 * TOON (Token-Oriented Object Notation) encode/decode.
 *
 * TOON is a compact, lossless, indentation-based *encoding of the JSON data
 * model*. Every JSON value has exactly one TOON form and every TOON document
 * decodes back to an equal JSON value (object key order excepted, as in JSON).
 *
 * These functions are thin wrappers over the installed `libjson2toon` reference
 * library (the conformance oracle); zsv does not implement its own encoder or
 * decoder. The library preserves the JSON scalar type as produced: a JSON
 * string that looks like a number (e.g. "1000") stays a string; real
 * numbers/booleans are preserved. No re-typing occurs in either direction.
 */

#ifndef ZSV_TOON_H
#define ZSV_TOON_H

#include <stdio.h>
#include <stddef.h>

/* Default number of spaces of indentation per nesting level (the libjson2toon
 * default). The `help toon` topic documents the grammar; json2toon_version()
 * reports the authoritative reference-library version. */
#define ZSV_TOON_DEFAULT_INDENT 2

/**
 * Encode a single JSON document, read from `in` (to EOF), as TOON written to
 * `out`. `indent` is the number of spaces per nesting level; values <= 0 use
 * ZSV_TOON_DEFAULT_INDENT.
 *
 * @return 0 on success; nonzero on JSON parse error or I/O error (a diagnostic
 *         is written to stderr).
 */
int zsv_json_to_toon(FILE *in, FILE *out, int indent);

/**
 * Same as zsv_json_to_toon(), but the JSON source is the in-memory buffer
 * [json, json+len).
 */
int zsv_json_to_toon_str(const unsigned char *json, size_t len, FILE *out, int indent);

/**
 * Decode a single TOON document, read from `in` (to EOF), to JSON written to
 * `out`. If `compact` is nonzero, emit compact JSON; otherwise pretty-print.
 *
 * @return 0 on success; nonzero on TOON parse error or I/O error (a diagnostic
 *         is written to stderr).
 */
int zsv_toon_to_json(FILE *in, FILE *out, int compact);

/**
 * Same as zsv_toon_to_json(), but the TOON source is the in-memory buffer
 * [toon, toon+len).
 */
int zsv_toon_to_json_str(const unsigned char *toon, size_t len, FILE *out, int compact);

#endif
