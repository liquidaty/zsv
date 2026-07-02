/* json2toon - JSON -> TOON conversion (forward direction).
 *
 * A streaming YAJL JSON parser drives the toonwriter push API: this file is a
 * thin SAX->writer adapter. toonwriter owns every TOON concern -- the "[N]"
 * count, inline/tabular/list classification, string/key quoting, and the bounded
 * RAM-then-temp-file store that buffers one array at a time. So peak memory is
 * bounded by YAJL's parse stack plus toonwriter's lookahead window (spilling to
 * disk past it), independent of document size; numbers pass through verbatim
 * (YAJL's raw-lexeme callback), and duplicate keys are rejected by toonwriter on
 * the buffered (in-array) path exactly as before.
 *
 * The reverse direction (toon2json.c) is the mirror image: a vendored yatl TOON
 * SAX parser drives the jsonwriter push API.
 */
#include "internal.h"

#include <toonwriter.h>
#include <yajl/yajl_parse.h>
#include <yatl_encode.h>       /* yatl_validate_utf8: shared UTF-8 gate (see check_utf8) */

#include <stdlib.h>
#include <string.h>

struct json2toon {
  json2toon_options opt;

  json2toon_sink sink;     /* user output sink + ctx (driven via tw_sink) */
  void *sink_ctx;
  int sink_err;            /* the sink reported a write failure */

  toonwriter_handle tw;    /* the TOON writer */
  yajl_handle h;           /* the JSON parser */
  int yajl_oom;            /* the YAJL allocator hit OOM */

  int err;                 /* sticky JSON2TOON_* */
  size_t err_off;

  size_t total_in;         /* bytes fed across completed feeds */
  size_t feed_base;        /* total_in at the start of the current yajl_parse */
  int done;                /* json2toon_finish has run */

  size_t depth;            /* open containers (objects + arrays): max_depth */
  unsigned arr_depth;      /* open arrays; >0 == inside a buffered array */
  uint64_t arr_start;      /* absolute offset where the outermost array opened */
};

typedef struct json2toon json2toon;

/* --------------------------------------------------------------- utilities */

static void set_err(json2toon *j, int code, size_t off) {
  if (j->err == JSON2TOON_OK) {
    j->err = code;
    j->err_off = off;
  }
}

/* Absolute input offset at the current point of the parse. yajl_get_bytes_-
 * consumed is per-yajl_parse-call, so add the offset of the current chunk. */
static size_t cur_off(json2toon *j) {
  return j->feed_base + yajl_get_bytes_consumed(j->h);
}

/* Check toonwriter / sink state after a write. Returns 1 to continue the parse,
 * 0 to cancel it (after recording the mapped error). */
static int tw_ok(json2toon *j) {
  if (j->sink_err) {
    set_err(j, JSON2TOON_ERR_IO, cur_off(j));
    return 0;
  }
  switch (toonwriter_error(j->tw)) {
  case toonwriter_status_ok:
    return 1;
  case toonwriter_status_out_of_memory:
    set_err(j, JSON2TOON_ERR_MEMORY, cur_off(j));
    return 0;
  case toonwriter_status_io_error: /* spill temp-file failure */
    set_err(j, JSON2TOON_ERR_IO, cur_off(j));
    return 0;
  default: /* invalid_value (duplicate key), etc. -> malformed for TOON */
    set_err(j, JSON2TOON_ERR_PARSE, cur_off(j));
    return 0;
  }
}

/* Reject string/key content that is not well-formed UTF-8. json2toon must not
 * accept a document it cannot losslessly round-trip: the TOON it emits is re-
 * parsed by toon2json, whose yatl SAX parser validates UTF-8 with this very
 * function. Validating here -- on the input side, with the reverse path's own
 * validator -- makes the two directions agree by construction. YAJL's built-in
 * check is weaker (it passes overlong forms, surrogates and code points above
 * U+10FFFF). Returns 1 if valid; else records a parse error and returns 0. */
static int check_utf8(json2toon *j, const unsigned char *s, size_t n) {
  if (yatl_validate_utf8(s, n))
    return 1;
  set_err(j, JSON2TOON_ERR_PARSE, cur_off(j));
  return 0;
}

/* max_array_bytes guard: enforced over the raw span of the outermost array,
 * mirroring the previous store-size limit. Returns 0 (stop) when exceeded. */
static int within_limit(json2toon *j) {
  if (j->arr_depth && j->opt.max_array_bytes &&
      cur_off(j) - j->arr_start > j->opt.max_array_bytes) {
    set_err(j, JSON2TOON_ERR_LIMIT, cur_off(j));
    return 0;
  }
  return 1;
}

/* Enter a container; enforce max_depth (objects and arrays alike). */
static int enter(json2toon *j) {
  if (j->depth + 1 > j->opt.max_depth) {
    set_err(j, JSON2TOON_ERR_DEPTH, cur_off(j));
    return 0;
  }
  j->depth++;
  return 1;
}

/* ----------------------------------------------------------- YAJL callbacks */

static int cb_null(void *c) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j)) return 0;
  toonwriter_null(j->tw);
  return tw_ok(j);
}
static int cb_bool(void *c, int b) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j)) return 0;
  toonwriter_bool(j->tw, (unsigned char)(b ? 1 : 0));
  return tw_ok(j);
}
static int cb_number(void *c, const char *s, size_t n) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j)) return 0;
  toonwriter_write_raw(j->tw, (const unsigned char *)s, n); /* verbatim lexeme */
  return tw_ok(j);
}
static int cb_string(void *c, const unsigned char *s, size_t n) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j) || !check_utf8(j, s, n)) return 0;
  toonwriter_strn(j->tw, s, n);
  return tw_ok(j);
}
static int cb_map_key(void *c, const unsigned char *s, size_t n) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j) || !check_utf8(j, s, n)) return 0;
  /* Pass NULL (not "") for a zero-length key: object_keyn treats (NULL,0) as
   * the empty key and never strlen()s YAJL's non-terminated buffer. */
  toonwriter_object_keyn(j->tw, n ? (const char *)s : NULL, n);
  return tw_ok(j);
}
static int cb_start_map(void *c) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j) || !enter(j)) return 0;
  toonwriter_start_object(j->tw);
  return tw_ok(j);
}
static int cb_start_array(void *c) {
  json2toon *j = (json2toon *)c;
  if (!within_limit(j) || !enter(j)) return 0;
  if (j->arr_depth++ == 0)
    j->arr_start = cur_off(j); /* outermost array: start byte accounting */
  toonwriter_start_array(j->tw);
  return tw_ok(j);
}
static int cb_end_map(void *c) {
  json2toon *j = (json2toon *)c;
  j->depth--;
  toonwriter_end(j->tw);
  return tw_ok(j);
}
static int cb_end_array(void *c) {
  json2toon *j = (json2toon *)c;
  j->depth--;
  if (j->arr_depth)
    j->arr_depth--;
  toonwriter_end(j->tw);
  return tw_ok(j);
}

static const yajl_callbacks J2T_CALLBACKS = {
  cb_null, cb_bool,
  NULL, NULL,        /* integer/double off: yajl_number gives the raw lexeme */
  cb_number, cb_string,
  cb_start_map, cb_map_key, cb_end_map,
  cb_start_array, cb_end_array,
  NULL               /* yajl_error (vendored extension): unused */
};

/* ------------------------------------------------------------ output sink */

/* fwrite-shaped adapter handed to toonwriter; forwards to the user sink. */
static size_t tw_sink(const void *restrict p, size_t sz, size_t nm,
                      void *restrict ctx) {
  json2toon *j = (json2toon *)ctx;
  size_t n = sz * nm;
  if (n && j->sink((const char *)p, n, j->sink_ctx) != 0) {
    j->sink_err = 1;
    return 0; /* signals a short write to toonwriter */
  }
  return nm;
}

/* ------------------------------------------------- YAJL allocator (OOM aware) */

static void *y_malloc(void *c, size_t n) {
  void *p = malloc(n);
  if (!p) ((json2toon *)c)->yajl_oom = 1;
  return p;
}
static void *y_realloc(void *c, void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) ((json2toon *)c)->yajl_oom = 1;
  return q;
}
static void y_free(void *c, void *p) { (void)c; free(p); }

/* ------------------------------------------------------------------- feed */

int json2toon_feed(json2toon *j, const char *data, size_t len) {
  yajl_status st;

  if (j->err != JSON2TOON_OK)
    return j->err;
  if (j->done) {
    set_err(j, JSON2TOON_ERR_USAGE, j->total_in);
    return j->err;
  }

  j->feed_base = j->total_in;
  st = yajl_parse(j->h, (const unsigned char *)data, len);
  j->total_in += len;

  if (st != yajl_status_ok) {
    /* A cancelled callback already recorded the precise error; otherwise this
     * is a YAJL lex/parse error (or an allocator OOM). */
    if (j->err == JSON2TOON_OK)
      set_err(j, j->yajl_oom ? JSON2TOON_ERR_MEMORY : JSON2TOON_ERR_PARSE,
              j->feed_base + yajl_get_bytes_consumed(j->h));
    return j->err;
  }
  if (j->sink_err) {
    set_err(j, JSON2TOON_ERR_IO, j->total_in);
    return j->err;
  }
  return JSON2TOON_OK;
}

/* ----------------------------------------------------------------- finish */

int json2toon_finish(json2toon *j) {
  yajl_status st;

  if (j->err != JSON2TOON_OK)
    return j->err;
  if (j->done)
    return JSON2TOON_OK;
  j->done = 1;

  st = yajl_complete_parse(j->h);
  if (st != yajl_status_ok && j->err == JSON2TOON_OK)
    set_err(j, j->yajl_oom ? JSON2TOON_ERR_MEMORY : JSON2TOON_ERR_PARSE,
            j->total_in);
  if (j->err != JSON2TOON_OK)
    return j->err;

  toonwriter_flush(j->tw);
  if (j->sink_err) {
    set_err(j, JSON2TOON_ERR_IO, j->total_in);
    return j->err;
  }
  if (toonwriter_error(j->tw) != toonwriter_status_ok) {
    set_err(j, toonwriter_error(j->tw) == toonwriter_status_io_error
                   ? JSON2TOON_ERR_IO
                   : JSON2TOON_ERR_MEMORY,
            j->total_in);
    return j->err;
  }
  return JSON2TOON_OK;
}

/* -------------------------------------------------------------- lifecycle */

json2toon *json2toon_new(json2toon_sink sink, void *ctx,
                         const json2toon_options *opts) {
  json2toon *j;
  struct toonwriter_opts two;
  yajl_alloc_funcs af;

  if (!sink)
    return NULL;
  j = (json2toon *)calloc(1, sizeof *j);
  if (!j)
    return NULL;

  j->opt.indent = 2;
  j->opt.max_depth = 128;
  j->opt.max_array_bytes = 0;              /* 0 == unlimited */
  j->opt.lookahead_buffer_size = 1u << 20; /* 1 MiB */
  j->opt.get_temp_filename = NULL;         /* NULL == tmpfile() */
  if (opts) {
    if (opts->indent) j->opt.indent = opts->indent;
    if (opts->max_depth) j->opt.max_depth = opts->max_depth;
    j->opt.max_array_bytes = opts->max_array_bytes;
    if (opts->lookahead_buffer_size)
      j->opt.lookahead_buffer_size = opts->lookahead_buffer_size;
    j->opt.get_temp_filename = opts->get_temp_filename;
  }
  j->sink = sink;
  j->sink_ctx = ctx;

  memset(&two, 0, sizeof two);
  two.max_buffer_size = j->opt.lookahead_buffer_size;
  two.get_temp_filename = j->opt.get_temp_filename;
  two.indent = j->opt.indent;
  j->tw = toonwriter_new_stream(tw_sink, j, &two);
  if (!j->tw) {
    free(j);
    return NULL;
  }

  af.malloc = y_malloc;
  af.realloc = y_realloc;
  af.free = y_free;
  af.ctx = j;
  j->h = yajl_alloc(&J2T_CALLBACKS, &af, j);
  if (!j->h) {
    toonwriter_delete(j->tw);
    free(j);
    return NULL;
  }
  /* String/key UTF-8 is validated in the SAX callbacks (see check_utf8), which
   * closes the gaps in YAJL's built-in check so the emitted TOON always re-parses
   * (README: "validated on the input side"). */
  return j;
}

void json2toon_delete(json2toon *j) {
  if (!j)
    return;
  if (j->h)
    yajl_free(j->h);
  if (j->tw)
    toonwriter_delete(j->tw);
  free(j);
}

size_t json2toon_error_offset(const json2toon *j) {
  return j ? j->err_off : 0;
}

const char *json2toon_strerror(int rc) {
  return j2t_strerror(rc, "malformed JSON input");
}

const char *json2toon_version(void) { return JSON2TOON_VERSION; }
