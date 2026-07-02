/* json2toon - TOON -> JSON conversion (the reverse direction).
 *
 * A streaming yatl TOON SAX parser drives the jsonwriter push API: a thin
 * parser->writer adapter, the mirror image of json2toon.c (a YAJL JSON parser
 * driving toonwriter). yatl is the sole TOON parser -- it owns line buffering,
 * the indentation pushdown, inline/tabular/list classification, string/number
 * decoding, UTF-8 validation and declared-count checks -- and reports the JSON
 * callback set (a tabular array arrives as an array of objects). jsonwriter owns
 * the JSON punctuation and escaping, flushing through t2j_sink_adapter into the
 * json2toon_sink in bounded pieces. Peak memory is bounded by nesting depth plus
 * one line, independent of document size; numbers pass through verbatim.
 *
 * The DEPTH and LIMIT guards live in this adapter -- depth in the container-start
 * callbacks, the per-line byte cap in feed() -- so each keeps its precise
 * JSON2TOON_* code; yatl's frame cap is raised past max_depth so it never trips
 * first.
 */
#include "internal.h"
#include "jsonwriter.h"

#include <yatl/yatl_parse.h>

#include <limits.h>
#include <stdlib.h>

#define T2J_DEFAULT_DEPTH 128
#define T2J_DEFAULT_LINE_BYTES (64u * 1024u * 1024u)

struct toon2json {
  jsonwriter_handle jw;    /* owns the output buffer + JSON punctuation/escaping */
  json2toon_sink sink;     /* user callback the jsonwriter handle flushes into */
  void *sinkctx;
  int sink_err;            /* sticky JSON2TOON_ERR_IO from the sink adapter */

  toon2json_options opt;

  yatl_handle h;           /* the TOON SAX parser (the sole parser) */
  int yatl_oom;            /* the yatl allocator hit OOM (MEMORY vs PARSE) */

  int err;                 /* sticky JSON2TOON_* */
  size_t err_offset;

  size_t total_in;         /* total input bytes fed to yatl */
  size_t line_len;         /* bytes in the current unterminated line (max_line_bytes) */
  unsigned depth;          /* open JSON containers (adapter-enforced max_depth) */
};

typedef struct toon2json toon2json;

/* --------------------------------------------------------------- utilities */

static void set_err(toon2json *t, int code, size_t off) {
  if (t->err == JSON2TOON_OK) {
    t->err = code;
    t->err_offset = off;
  }
}

/* ---------------------------------------------------------- JSON sink + emit */

/* fwrite-shaped adapter handed to jsonwriter_new_stream(): forwards the handle's
 * flushed bytes to the user sink. jsonwriter ignores the return value, so an IO
 * failure is recorded in t->sink_err and surfaced by feed/finish. A sticky error
 * suppresses writes, so a partial document never reaches the sink after one. */
static size_t t2j_sink_adapter(const void *restrict ptr, size_t size,
                               size_t nmemb, void *restrict arg) {
  toon2json *t = (toon2json *)arg;
  size_t n = size * nmemb;             /* jsonwriter always calls with nmemb==1 */
  if (t->sink_err != JSON2TOON_OK || t->err != JSON2TOON_OK)
    return 0;
  if (n && t->sink((const char *)ptr, n, t->sinkctx) != 0) {
    t->sink_err = JSON2TOON_ERR_IO;
    return 0;
  }
  return nmemb;
}

/* Post-emit check after a jsonwriter call in a callback. Returns 1 to continue
 * the parse, 0 to cancel it (an IO failure was recorded; the exact offset is
 * refined once yatl_parse returns). */
static int t2j_ok(toon2json *t) {
  if (t->sink_err != JSON2TOON_OK) {
    set_err(t, JSON2TOON_ERR_IO, t->total_in);
    return 0;
  }
  return 1;
}

/* Enter a JSON container; enforce max_depth here (not in yatl) so the overflow
 * keeps the JSON2TOON_ERR_DEPTH code. */
static int t2j_enter(toon2json *t) {
  if (t->depth + 1 > t->opt.max_depth) {
    set_err(t, JSON2TOON_ERR_DEPTH, t->total_in);   /* offset refined post-parse */
    return 0;
  }
  t->depth++;
  return 1;
}

/* ------------------------------------------------------------ yatl callbacks */

static int cb_null(void *c) {
  toon2json *t = (toon2json *)c;
  jsonwriter_null(t->jw);
  return t2j_ok(t);
}
static int cb_bool(void *c, int b) {
  toon2json *t = (toon2json *)c;
  jsonwriter_bool(t->jw, (unsigned char)(b ? 1 : 0));
  return t2j_ok(t);
}
static int cb_number(void *c, const char *s, size_t n) {
  toon2json *t = (toon2json *)c;
  jsonwriter_write_raw(t->jw, (const unsigned char *)s, n); /* verbatim lexeme */
  return t2j_ok(t);
}
static int cb_string(void *c, const unsigned char *s, size_t n) {
  toon2json *t = (toon2json *)c;
  jsonwriter_strn(t->jw, n ? s : (const unsigned char *)"", n);
  return t2j_ok(t);
}
static int cb_map_key(void *c, const unsigned char *s, size_t n) {
  toon2json *t = (toon2json *)c;
  /* jsonwriter_object_keyn treats a zero length as "call strlen", wrong for our
   * non-NUL-terminated buffers, so an empty key routes through the literal "". */
  if (n == 0)
    jsonwriter_object_keyn(t->jw, "", 0);
  else
    jsonwriter_object_keyn(t->jw, (const char *)s, n);
  return t2j_ok(t);
}
/* Open a container: enter() enforces max_depth, then jsonwriter opens the
 * object/array (its only failure is a nesting-array realloc OOM). */
static int t2j_start(toon2json *t, int obj) {
  if (!t2j_enter(t)) return 0;
  if ((obj ? jsonwriter_start_object(t->jw) : jsonwriter_start_array(t->jw)) != 0) {
    set_err(t, JSON2TOON_ERR_MEMORY, t->total_in);
    return 0;
  }
  return t2j_ok(t);
}
static int cb_start_map(void *c)   { return t2j_start((toon2json *)c, 1); }
static int cb_start_array(void *c) { return t2j_start((toon2json *)c, 0); }

/* jsonwriter_end closes either container, so one callback serves both events. */
static int cb_end(void *c) {
  toon2json *t = (toon2json *)c;
  if (t->depth) t->depth--;
  jsonwriter_end(t->jw);
  return t2j_ok(t);
}

static const yatl_callbacks T2J_CALLBACKS = {
  cb_null, cb_bool,
  NULL, NULL,        /* integer/double off: yatl_number gives the raw lexeme */
  cb_number, cb_string,
  cb_start_map, cb_map_key, cb_end,
  cb_start_array, cb_end,
  NULL               /* yatl_error: unused (the verbatim sink cannot overflow) */
};

/* ------------------------------------------------- yatl allocator (OOM aware) */

static void *ya_malloc(void *c, size_t n) {
  void *p = malloc(n);
  if (!p) ((toon2json *)c)->yatl_oom = 1;
  return p;
}
static void *ya_realloc(void *c, void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) ((toon2json *)c)->yatl_oom = 1;
  return q;
}
static void ya_free(void *c, void *p) { (void)c; free(p); }

/* ------------------------------------------------------------------- feed */

/* Push [data,len) to yatl and map its status. `base` is the stream offset of the
 * first byte. */
static int push_yatl(toon2json *t, const char *data, size_t len, size_t base) {
  yatl_status st = yatl_parse(t->h, (const unsigned char *)data, len);
  if (st != yatl_status_ok) {
    /* yatl reports the error position as a per-chunk offset, so add the chunk's
     * base to recover the absolute stream offset. */
    size_t off = base + yatl_get_bytes_consumed(t->h);
    if (t->err == JSON2TOON_OK)  /* yatl's own lex/parse error (or allocator OOM) */
      set_err(t, t->yatl_oom ? JSON2TOON_ERR_MEMORY : JSON2TOON_ERR_PARSE, off);
    else                         /* a callback recorded DEPTH / IO: refine offset */
      t->err_offset = off;
    return t->err;
  }
  if (t->sink_err != JSON2TOON_OK) {
    set_err(t, t->sink_err, base + len);
    return t->err;
  }
  return JSON2TOON_OK;
}

int toon2json_feed(toon2json *t, const char *data, size_t len) {
  size_t base = t->total_in, i;

  if (t->err != JSON2TOON_OK)
    return t->err;

  /* Enforce max_line_bytes before yatl buffers the line: track the current
   * (unterminated) line's length across feeds; a line over the cap is
   * JSON2TOON_ERR_LIMIT. Keeps peak memory bounded to nesting depth plus one line
   * (yatl buffers only up to the current newline). */
  if (t->opt.max_line_bytes) {
    for (i = 0; i < len; i++) {
      if (data[i] == '\n') {
        t->line_len = 0;
      } else if (t->line_len + 1 > t->opt.max_line_bytes) {
        if (i)                             /* feed the in-limit prefix, if any */
          push_yatl(t, data, i, base);
        t->total_in = base + i;
        if (t->err == JSON2TOON_OK)        /* a parse/mem error on the prefix wins */
          set_err(t, JSON2TOON_ERR_LIMIT, base + i);
        return t->err;
      } else {
        t->line_len++;
      }
    }
  }

  push_yatl(t, data, len, base);
  t->total_in = base + len;
  return t->err;
}

/* ----------------------------------------------------------------- finish */

int toon2json_finish(toon2json *t) {
  yatl_status st;

  if (t->err != JSON2TOON_OK)
    return t->err;

  /* Flush the final unterminated line, close open containers, and emit "{}" for
   * an empty document -- all driven through the callbacks above. */
  st = yatl_complete_parse(t->h);
  if (st != yatl_status_ok && t->err == JSON2TOON_OK)
    set_err(t, t->yatl_oom ? JSON2TOON_ERR_MEMORY : JSON2TOON_ERR_PARSE,
            yatl_get_bytes_consumed(t->h));   /* absolute offset at finish */
  if (t->err != JSON2TOON_OK)
    return t->err;

  jsonwriter_flush(t->jw);
  if (t->sink_err != JSON2TOON_OK) {
    set_err(t, t->sink_err, t->total_in);
    return t->err;
  }
  return JSON2TOON_OK;
}

/* -------------------------------------------------------------- lifecycle */

toon2json *toon2json_new(json2toon_sink sink, void *ctx,
                         const toon2json_options *opts) {
  toon2json *t;
  yatl_alloc_funcs af;
  size_t need;

  if (!sink)
    return NULL;
  t = (toon2json *)calloc(1, sizeof *t);
  if (!t)
    return NULL;

  t->opt.max_depth = T2J_DEFAULT_DEPTH;
  t->opt.max_line_bytes = T2J_DEFAULT_LINE_BYTES;
  if (opts) {
    if (opts->max_depth)
      t->opt.max_depth = opts->max_depth;
    if (opts->max_line_bytes)
      t->opt.max_line_bytes = opts->max_line_bytes;
    t->opt.lenient = opts->lenient;
  }

  t->sink = sink;
  t->sinkctx = ctx;
  t->sink_err = JSON2TOON_OK;
  t->err = JSON2TOON_OK;

  t->jw = jsonwriter_new_stream(t2j_sink_adapter, t);
  if (!t->jw) {
    free(t);
    return NULL;
  }
  jsonwriter_set_option(t->jw, jsonwriter_option_compact);

  /* Keep the JSON writer's and the TOON parser's nesting caps out of the way of
   * this adapter's own max_depth enforcement. The writer can sit one level below
   * the deepest open container -- a tabular row or inline-array element opens a
   * container transiently under it -- so it needs max_depth + 2 slots; yatl's
   * frame cap is raised to the same value so it never trips before t2j_enter()
   * records ERR_DEPTH. The writer's cap must be set now, while it is still at
   * depth 0 (as the API requires). */
  need = (size_t)t->opt.max_depth + 2;
  if (need < (size_t)t->opt.max_depth ||          /* overflow guard */
      (need > JSONWRITER_MAX_NESTING &&
       (need > UINT_MAX ||
        jsonwriter_set_max_nesting(t->jw, (unsigned int)need) !=
            jsonwriter_status_ok))) {
    jsonwriter_delete(t->jw);
    free(t);
    return NULL;
  }

  af.malloc = ya_malloc;
  af.realloc = ya_realloc;
  af.free = ya_free;
  af.ctx = t;
  t->h = yatl_alloc(&T2J_CALLBACKS, &af, t);
  if (!t->h) {
    jsonwriter_delete(t->jw);
    free(t);
    return NULL;
  }
  /* Raise yatl's frame cap past this adapter's max_depth (see above); `need` is
   * <= UINT_MAX here (larger values already failed the writer guard). */
  yatl_config(t->h, yatl_max_depth, (unsigned)need);
  /* Strict bare-string handling unless the caller opted into lenient mode. UTF-8
   * validation stays on (yatl's default): invalid UTF-8 in TOON string content
   * is rejected, keeping the reverse path from emitting bytes the JSON writer
   * could not losslessly carry. */
  if (t->opt.lenient)
    yatl_config(t->h, yatl_lenient_scalars, 1);

  return t;
}

void toon2json_delete(toon2json *t) {
  if (!t)
    return;
  if (t->h)
    yatl_free(t->h);
  jsonwriter_delete(t->jw);
  free(t);
}

size_t toon2json_error_offset(const toon2json *t) {
  return t ? t->err_offset : 0;
}

const char *toon2json_strerror(int rc) {
  return j2t_strerror(rc, "malformed TOON input");
}
