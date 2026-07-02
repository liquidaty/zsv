/* json2toon - stdio / convenience layer.
 *
 * Codec-agnostic glue over the public push API: a FILE sink, fwrite(3)-shaped
 * feed adapters, and whole-FILE / whole-buffer one-shot converters. Built only
 * on the push API, so the streaming guarantee holds (input pumped in fixed
 * chunks, output via the sink); no internal type is referenced.
 *
 * Forward and reverse differ only in which push functions they call, so the
 * drivers are written once against a codec_ops vtable of correctly-typed
 * trampolines -- which also avoids the UB of calling, say, json2toon_feed()
 * through an incompatible function-pointer type.
 */
#include "json2toon.h"

#include <stdio.h>

/* FILE-reader chunk; modest so the stack buffer is safe even on emscripten. */
#define J2T_CONVERT_CHUNK 16384

/* ------------------------------------------------------------------ vtable */

/* Uniform void*-converter ops; each trampoline forwards to the typed function,
 * keeping the cast inside a correctly-typed function (no fn-pointer-cast UB). */
typedef struct {
  int    (*feed)(void *conv, const char *data, size_t len);
  int    (*finish)(void *conv);
  size_t (*error_offset)(const void *conv);
  void   (*destroy)(void *conv);
} codec_ops;

static int    j2t_feed_v(void *c, const char *d, size_t n) { return json2toon_feed((json2toon_t *)c, d, n); }
static int    j2t_finish_v(void *c) { return json2toon_finish((json2toon_t *)c); }
static size_t j2t_eoff_v(const void *c) { return json2toon_error_offset((const json2toon_t *)c); }
static void   j2t_destroy_v(void *c) { json2toon_delete((json2toon_t *)c); }

static int    t2j_feed_v(void *c, const char *d, size_t n) { return toon2json_feed((toon2json_t *)c, d, n); }
static int    t2j_finish_v(void *c) { return toon2json_finish((toon2json_t *)c); }
static size_t t2j_eoff_v(const void *c) { return toon2json_error_offset((const toon2json_t *)c); }
static void   t2j_destroy_v(void *c) { toon2json_delete((toon2json_t *)c); }

static const codec_ops J2T_OPS = { j2t_feed_v, j2t_finish_v, j2t_eoff_v, j2t_destroy_v };
static const codec_ops T2J_OPS = { t2j_feed_v, t2j_finish_v, t2j_eoff_v, t2j_destroy_v };

/* -------------------------------------------------------------- FILE sink */

int json2toon_file_sink(const char *data, size_t len, void *file) {
  FILE *f = (FILE *)file;
  if (len == 0)                                  /* nothing to write */
    return 0;
  return fwrite(data, 1, len, f) == len ? 0 : -1;
}

/* ------------------------------------------------------- fwrite feed adapter */

static size_t feed_fwrite_impl(const void *ptr, size_t size, size_t nmemb,
                               void *conv, const codec_ops *ops) {
  size_t total;
  if (size && nmemb > (size_t)-1 / size)
    return 0;                                  /* size*nmemb would overflow */
  total = size * nmemb;
  if (total && ops->feed(conv, (const char *)ptr, total) != JSON2TOON_OK)
    return 0;                                  /* poisoned: tell producer to stop */
  return nmemb;                                /* incl. the size==0 / nmemb==0 no-op */
}

size_t json2toon_feed_fwrite(const void *ptr, size_t size, size_t nmemb, void *j2t) {
  return feed_fwrite_impl(ptr, size, nmemb, j2t, &J2T_OPS);
}

size_t toon2json_feed_fwrite(const void *ptr, size_t size, size_t nmemb, void *t2j) {
  return feed_fwrite_impl(ptr, size, nmemb, t2j, &T2J_OPS);
}

/* ----------------------------------------------------------- whole-doc drivers */

/* Shared driver tail: finish, flush (so a buffered write failure surfaces as
 * ERR_IO rather than being lost), capture the error offset, destroy. `rc` is OK
 * unless the feed phase already failed. */
static int finalize(void *conv, FILE *out, const codec_ops *ops, int rc,
                    size_t *error_offset) {
  if (rc == JSON2TOON_OK)
    rc = ops->finish(conv);
  if (rc == JSON2TOON_OK && fflush(out) != 0)
    rc = JSON2TOON_ERR_IO;                      /* deferred write failure */
  if (rc != JSON2TOON_OK && error_offset)
    *error_offset = ops->error_offset(conv);
  ops->destroy(conv);
  return rc;
}

static int drive_file(void *conv, FILE *in, FILE *out, const codec_ops *ops,
                      size_t *error_offset) {
  char buf[J2T_CONVERT_CHUNK];
  size_t n;
  int rc = JSON2TOON_OK;

  while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
    rc = ops->feed(conv, buf, n);
    if (rc != JSON2TOON_OK)
      break;
  }
  /* fread returning < requested means EOF or error; ferror() is the authority. */
  if (rc == JSON2TOON_OK && ferror(in))
    rc = JSON2TOON_ERR_IO;
  return finalize(conv, out, ops, rc, error_offset);
}

static int drive_mem(void *conv, const char *buf, size_t len, FILE *out,
                     const codec_ops *ops, size_t *error_offset) {
  int rc = len ? ops->feed(conv, buf, len) : JSON2TOON_OK;
  return finalize(conv, out, ops, rc, error_offset);
}

/* ---------------------------------------------------------- public converters */

int json2toon_convert_file(FILE *in, FILE *out, const json2toon_options *opts,
                           size_t *error_offset) {
  json2toon_t *j;
  if (error_offset)
    *error_offset = 0;
  j = json2toon_new(json2toon_file_sink, out, opts);
  if (!j)
    return JSON2TOON_ERR_MEMORY;
  return drive_file(j, in, out, &J2T_OPS, error_offset);
}

int toon2json_convert_file(FILE *in, FILE *out, const toon2json_options *opts,
                           size_t *error_offset) {
  toon2json_t *t;
  if (error_offset)
    *error_offset = 0;
  t = toon2json_new(json2toon_file_sink, out, opts);
  if (!t)
    return JSON2TOON_ERR_MEMORY;
  return drive_file(t, in, out, &T2J_OPS, error_offset);
}

int json2toon_convert_mem(const char *buf, size_t len, FILE *out,
                          const json2toon_options *opts, size_t *error_offset) {
  json2toon_t *j;
  if (error_offset)
    *error_offset = 0;
  if (!buf && len)
    return JSON2TOON_ERR_USAGE;
  j = json2toon_new(json2toon_file_sink, out, opts);
  if (!j)
    return JSON2TOON_ERR_MEMORY;
  return drive_mem(j, buf, len, out, &J2T_OPS, error_offset);
}

int toon2json_convert_mem(const char *buf, size_t len, FILE *out,
                          const toon2json_options *opts, size_t *error_offset) {
  toon2json_t *t;
  if (error_offset)
    *error_offset = 0;
  if (!buf && len)
    return JSON2TOON_ERR_USAGE;
  t = toon2json_new(json2toon_file_sink, out, opts);
  if (!t)
    return JSON2TOON_ERR_MEMORY;
  return drive_mem(t, buf, len, out, &T2J_OPS, error_offset);
}
