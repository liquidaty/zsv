/* json2toon - convert JSON to TOON (Token-Oriented Object Notation).
 *
 * Public C API. The converter is a streaming push parser: input is supplied in
 * arbitrary chunks via json2toon_feed(), and TOON output is delivered through a
 * sink callback in bounded-size pieces. Neither the whole input nor the whole
 * output is ever required to be resident in memory.
 *
 * The reverse direction (TOON -> JSON) is exposed through a mirror-image API
 * (toon2json_*) at the bottom of this header.
 *
 * Copyright (c) 2026. MIT License.
 */
#ifndef JSON2TOON_H
#define JSON2TOON_H

#include <stddef.h>
#include <stdio.h>   /* FILE, for the stdio convenience layer at the bottom */

#ifdef __cplusplus
extern "C" {
#endif

/* Library version (single source of truth; configure and json2toon_version()
 * both derive from it). The numeric form gates a minimum floor at compile time:
 *
 *   #if !defined(JSON2TOON_VERSION_NUMBER) || JSON2TOON_VERSION_NUMBER < 10100
 *   # error "json2toon >= 1.1.0 required (stdio convenience layer)"
 *   #endif
 */
#define JSON2TOON_VERSION_MAJOR 1
#define JSON2TOON_VERSION_MINOR 0
#define JSON2TOON_VERSION_PATCH 0
#define JSON2TOON_VERSION "1.0.0"
#define JSON2TOON_VERSION_NUMBER                                          \
  (JSON2TOON_VERSION_MAJOR * 10000 + JSON2TOON_VERSION_MINOR * 100 +       \
   JSON2TOON_VERSION_PATCH)

/* Public-symbol decoration (centralizes the header's only platform divergence).
 * The library builds with JSON2TOON_BUILD to export the annotated symbols
 * (with -fvisibility=hidden hiding the rest). Static consumers need no define;
 * Windows DLL consumers define JSON2TOON_DLL to import. */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(JSON2TOON_BUILD)
#    define JSON2TOON_API __declspec(dllexport)
#  elif defined(JSON2TOON_DLL)
#    define JSON2TOON_API __declspec(dllimport)
#  else
#    define JSON2TOON_API           /* static linking (default) */
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define JSON2TOON_API __attribute__((visibility("default")))
#else
#  define JSON2TOON_API
#endif

/* Status / error codes. JSON2TOON_OK is zero; all errors are negative.
 *
 * The code list is centralized in one X-macro: the single source of truth for
 * each code's symbol, numeric value, and default message. The enum below and
 * the shared stringifier (src/format.c) are both generated from it, so adding a
 * code is a one-line edit that cannot desync. The default JSON2TOON_ERR_PARSE
 * message is replaced per direction ("JSON" vs "TOON") by the *_strerror()
 * wrappers and so is never surfaced. (Requires a C99+/C++11+ compiler.) */
#define JSON2TOON_ERROR_LIST(X)                                              \
  X(JSON2TOON_OK,          0, "success")                                     \
  X(JSON2TOON_ERR_PARSE,  -1, "malformed input")     /* per-direction wording */ \
  X(JSON2TOON_ERR_IO,     -2, "output write error")  /* sink returned non-zero */ \
  X(JSON2TOON_ERR_MEMORY, -3, "out of memory")       /* allocation failed */ \
  X(JSON2TOON_ERR_DEPTH,  -4, "maximum nesting depth exceeded")             \
  X(JSON2TOON_ERR_LIMIT,  -5, "configured size limit exceeded")            \
  X(JSON2TOON_ERR_USAGE,  -6, "API misuse")          /* e.g. feed after error */

enum {
#define JSON2TOON_ENUM_ENTRY(name, value, msg) name = (value),
  JSON2TOON_ERROR_LIST(JSON2TOON_ENUM_ENTRY)
#undef JSON2TOON_ENUM_ENTRY
};

/* Sink callback. Receives a chunk of UTF-8 output. Return 0 to continue,
 * non-zero to abort the conversion with JSON2TOON_ERR_IO. The bytes are only
 * valid for the duration of the call. */
typedef int (*json2toon_sink)(const char *data, size_t len, void *ctx);

/* Configuration. Pass NULL to json2toon_new() to accept all defaults. A zero
 * field also selects the default for that field. */
typedef struct {
  unsigned indent;          /* spaces per indentation level (default 2) */
  unsigned max_depth;       /* maximum nesting depth (default 128) */
  size_t max_array_bytes;   /* cap on the raw size of a single buffered array,
                             * in bytes (default 0 == unlimited). Exceeding it
                             * is JSON2TOON_ERR_LIMIT. */
  size_t lookahead_buffer_size; /* RAM kept for a buffered array before the
                             * overflow spills to a temp file (default 0 == 1
                             * MiB). Tunes RAM-vs-disk only; never changes the
                             * output, which is byte-identical at any setting. */
  char *(*get_temp_filename)(const char *prefix); /* names the spill file:
                             * returns a malloc'd path the library fopen()s and,
                             * on cleanup, remove()s then free()s (so it must stay
                             * valid until cleanup). NULL (default) == tmpfile(). */
} json2toon_options;

typedef struct json2toon json2toon_t;

/* Create a converter. Returns NULL on allocation failure. */
JSON2TOON_API json2toon_t *json2toon_new(json2toon_sink sink, void *ctx,
                                         const json2toon_options *opts);

/* Feed a chunk of JSON input. May be called repeatedly. Returns JSON2TOON_OK
 * or a negative error code; once an error is returned the converter is poisoned
 * and further feed/finish calls return the same error. */
JSON2TOON_API int json2toon_feed(json2toon_t *j2t, const char *data, size_t len);

/* Signal end of input and flush any pending output. Returns JSON2TOON_OK on a
 * complete, well-formed document, otherwise a negative error code. */
JSON2TOON_API int json2toon_finish(json2toon_t *j2t);

/* Destroy a converter created by json2toon_new(). Safe to call with NULL. */
JSON2TOON_API void json2toon_delete(json2toon_t *j2t);

/* Byte offset within the input stream at which the most recent error was
 * detected. Meaningful only after a call returned a negative code. */
JSON2TOON_API size_t json2toon_error_offset(const json2toon_t *j2t);

/* Human-readable description of a status code. */
JSON2TOON_API const char *json2toon_strerror(int rc);

/* Library version string, e.g. "1.0.0". */
JSON2TOON_API const char *json2toon_version(void);

/* ====================================================================== *
 *  TOON -> JSON (the reverse direction)
 *
 *  Mirrors the json2toon API exactly: a converter object fed incrementally
 *  via toon2json_feed() and flushed with toon2json_finish(), delivering JSON
 *  output through the same json2toon_sink callback in bounded-size pieces.
 *  TOON is indentation-structured, so the reverse converter is driven line by
 *  line; peak memory is bounded by the nesting depth and the width of the
 *  widest single line (e.g. a tabular row), never by total input or output.
 *
 *  Output is compact (no insignificant whitespace) UTF-8 JSON. Numbers are
 *  passed through verbatim and the encoding round-trips losslessly back to the
 *  TOON that json2toon would have produced.
 * ====================================================================== */

/* Configuration for the reverse converter. Pass NULL to accept all defaults;
 * a zero field also selects that field's default. */
typedef struct {
  unsigned max_depth;       /* maximum nesting depth (default 128) */
  size_t max_line_bytes;    /* cap on a single buffered input line
                             * (default 0 == a large built-in limit) */
  int lenient;              /* if non-zero, accept any unquoted value as a bare
                             * string instead of rejecting tokens that the
                             * forward path would have quoted (default 0:
                             * strict, so non-TOON junk is a parse error) */
} toon2json_options;

typedef struct toon2json toon2json_t;

/* Create a reverse converter. Returns NULL on allocation failure. */
JSON2TOON_API toon2json_t *toon2json_new(json2toon_sink sink, void *ctx,
                                         const toon2json_options *opts);

/* Feed a chunk of TOON input. May be called repeatedly. Returns JSON2TOON_OK
 * or a negative error code; once an error is returned the converter is poisoned
 * and further feed/finish calls return the same error. */
JSON2TOON_API int toon2json_feed(toon2json_t *t2j, const char *data, size_t len);

/* Signal end of input and flush any pending output. Returns JSON2TOON_OK on a
 * complete, well-formed document, otherwise a negative error code. */
JSON2TOON_API int toon2json_finish(toon2json_t *t2j);

/* Destroy a converter created by toon2json_new(). Safe to call with NULL. */
JSON2TOON_API void toon2json_delete(toon2json_t *t2j);

/* Byte offset within the input stream at which the most recent error was
 * detected. Meaningful only after a call returned a negative code. */
JSON2TOON_API size_t toon2json_error_offset(const toon2json_t *t2j);

/* Human-readable description of a status code (TOON-oriented wording). */
JSON2TOON_API const char *toon2json_strerror(int rc);

/* ====================================================================== *
 *  stdio / convenience layer
 *
 *  Codec-agnostic glue on the push API; preserves the streaming guarantee. One
 *  FILE sink serves both directions (both converters take a json2toon_sink).
 *
 *  FILE ownership: these never open or close `in` / `out` (the caller owns
 *  both). The whole-document converters flush `out` on success, so a buffered
 *  write failure surfaces as JSON2TOON_ERR_IO; the caller still fclose()s.
 * ====================================================================== */

/* Sink that writes each output chunk to a stdio FILE. Pass as the `sink` to
 * json2toon_new() / toon2json_new() with `ctx` a writable FILE *. Returns 0, or
 * non-zero on a short write (surfaced by the converter as JSON2TOON_ERR_IO). */
JSON2TOON_API int json2toon_file_sink(const char *data, size_t len, void *file);

/* fwrite(3)-signature adapters: feed size*nmemb bytes at `ptr` into the
 * converter (`json2toon_t *` / `toon2json_t *`). Returns nmemb, or 0 once the
 * converter is in error -- a short count telling an fwrite-style producer to
 * stop. size==0 or nmemb==0 is a no-op returning nmemb; overflow returns 0. */
JSON2TOON_API size_t json2toon_feed_fwrite(const void *ptr, size_t size, size_t nmemb, void *j2t);
JSON2TOON_API size_t toon2json_feed_fwrite(const void *ptr, size_t size, size_t nmemb, void *t2j);

/* Convert all of `in` (to EOF) to the other format, written to `out`. `opts`
 * may be NULL. Returns JSON2TOON_OK or a negative JSON2TOON_ERR_*; on error,
 * *error_offset (if non-NULL) gets the input byte offset, or 0 for errors with
 * no position (allocation, or a flush-time IO error after the input was read).
 *
 * Empty input is not special-cased -- it takes each codec's natural meaning:
 * json2toon -> parse error (offset 0); toon2json -> the empty object ("{}",
 * JSON2TOON_OK). Callers wanting "produced nothing" == success handle it above. */
JSON2TOON_API int json2toon_convert_file(FILE *in, FILE *out, const json2toon_options *opts, size_t *error_offset);
JSON2TOON_API int toon2json_convert_file(FILE *in, FILE *out, const toon2json_options *opts, size_t *error_offset);

/* Convert the in-memory document [buf, buf+len) to `out`. Same return/offset and
 * empty-input semantics as the *_convert_file() forms. A NULL `buf` with len==0
 * is the empty document; a NULL `buf` with len>0 is a usage error
 * (JSON2TOON_ERR_USAGE). */
JSON2TOON_API int json2toon_convert_mem(const char *buf, size_t len, FILE *out, const json2toon_options *opts, size_t *error_offset);
JSON2TOON_API int toon2json_convert_mem(const char *buf, size_t len, FILE *out, const toon2json_options *opts, size_t *error_offset);

#ifdef __cplusplus
}
#endif

#endif /* JSON2TOON_H */
