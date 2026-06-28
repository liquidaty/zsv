/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 *
 * TOON encode/decode for zsv. This is thin plumbing over the installed
 * `libjson2toon` reference library (the conformance oracle named in the spec);
 * zsv does not implement its own TOON encoder or decoder. Both directions use
 * the library's streaming push-parser API, driven here from a FILE or a memory
 * buffer with a sink that writes to a FILE. Memory stays bounded by the data's
 * structure, not its size.
 */

#include <stdio.h>
#include <string.h>

#include <zsv/utils/toon.h>
#include <json2toon.h>

/* Sink: forward each output chunk to a FILE. Returns non-zero on write error,
 * which the converter reports as JSON2TOON_ERR_IO. */
struct zsv_toon_sink {
  FILE *f;
};

static int zsv_toon_file_sink(const char *data, size_t len, void *ctx) {
  struct zsv_toon_sink *s = ctx;
  return fwrite(data, 1, len, s->f) == len ? 0 : -1;
}

/* ----------------------------------------------------------- JSON -> TOON */

int zsv_json_to_toon_str(const unsigned char *json, size_t len, FILE *out, int indent) {
  struct zsv_toon_sink sink = {out};
  json2toon_options opts = {0};
  if (indent > 0)
    opts.indent = (unsigned)indent;
  json2toon_t *j = json2toon_new(zsv_toon_file_sink, &sink, &opts);
  if (!j) {
    fprintf(stderr, "toon: out of memory\n");
    return 1;
  }
  int rc = json2toon_feed(j, (const char *)json, len);
  if (rc == JSON2TOON_OK)
    rc = json2toon_finish(j);
  if (rc != JSON2TOON_OK)
    fprintf(stderr, "toon: %s (at byte %zu)\n", json2toon_strerror(rc), json2toon_error_offset(j));
  json2toon_delete(j);
  return rc == JSON2TOON_OK ? 0 : 1;
}

int zsv_json_to_toon(FILE *in, FILE *out, int indent) {
  struct zsv_toon_sink sink = {out};
  json2toon_options opts = {0};
  if (indent > 0)
    opts.indent = (unsigned)indent;
  json2toon_t *j = json2toon_new(zsv_toon_file_sink, &sink, &opts);
  if (!j) {
    fprintf(stderr, "toon: out of memory\n");
    return 1;
  }
  char buf[65536];
  size_t n;
  int rc = JSON2TOON_OK;
  while ((n = fread(buf, 1, sizeof buf, in)) > 0)
    if ((rc = json2toon_feed(j, buf, n)) != JSON2TOON_OK)
      break;
  if (rc == JSON2TOON_OK)
    rc = json2toon_finish(j);
  if (rc != JSON2TOON_OK)
    fprintf(stderr, "toon: %s (at byte %zu)\n", json2toon_strerror(rc), json2toon_error_offset(j));
  json2toon_delete(j);
  return rc == JSON2TOON_OK ? 0 : 1;
}

/* ----------------------------------------------------------- TOON -> JSON */
/* The library emits compact UTF-8 JSON; `compact` is accepted for API symmetry
 * but the output is always compact. */

int zsv_toon_to_json_str(const unsigned char *toon, size_t len, FILE *out, int compact) {
  (void)compact;
  struct zsv_toon_sink sink = {out};
  toon2json_t *t = toon2json_new(zsv_toon_file_sink, &sink, NULL);
  if (!t) {
    fprintf(stderr, "toon: out of memory\n");
    return 1;
  }
  int rc = toon2json_feed(t, (const char *)toon, len);
  if (rc == JSON2TOON_OK)
    rc = toon2json_finish(t);
  if (rc != JSON2TOON_OK)
    fprintf(stderr, "toon: %s (at byte %zu)\n", toon2json_strerror(rc), toon2json_error_offset(t));
  toon2json_delete(t);
  return rc == JSON2TOON_OK ? 0 : 1;
}

int zsv_toon_to_json(FILE *in, FILE *out, int compact) {
  (void)compact;
  struct zsv_toon_sink sink = {out};
  toon2json_t *t = toon2json_new(zsv_toon_file_sink, &sink, NULL);
  if (!t) {
    fprintf(stderr, "toon: out of memory\n");
    return 1;
  }
  char buf[65536];
  size_t n;
  int rc = JSON2TOON_OK;
  while ((n = fread(buf, 1, sizeof buf, in)) > 0)
    if ((rc = toon2json_feed(t, buf, n)) != JSON2TOON_OK)
      break;
  if (rc == JSON2TOON_OK)
    rc = toon2json_finish(t);
  if (rc != JSON2TOON_OK)
    fprintf(stderr, "toon: %s (at byte %zu)\n", toon2json_strerror(rc), toon2json_error_offset(t));
  toon2json_delete(t);
  return rc == JSON2TOON_OK ? 0 : 1;
}
