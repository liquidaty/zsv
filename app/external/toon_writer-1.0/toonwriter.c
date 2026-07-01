/* toonwriter - a small, fast, bounded-memory TOON (Token-Oriented Object
 * Notation) writer with a streaming push API, modeled after json_writer.
 *
 * Output conforms to the TOON spec (https://github.com/toon-format/spec):
 * objects emit one `key: value` line per member; arrays emit a `[N]` header and
 * one of three forms -- inline (`[N]: a,b,c`) when every element is a scalar,
 * tabular (`[N]{cols}:` + one row per line) when every element is a non-empty
 * object sharing one primitive-valued key set, or an expanded list (`[N]:` +
 * `- item` lines) otherwise.
 *
 * Bounded memory: TOON cannot emit an array header until the whole array is
 * seen (the count, and for tabular the column set, are only knowable at the
 * end). Objects need no such lookahead, so they stream directly to output;
 * each *array* is captured into a backing store (RAM up to opts.max_buffer_size,
 * then spilling to a temp file), classified, and emitted by streaming over that
 * store -- so peak heap is bounded by the configured window plus one row /
 * scalar / the column template, regardless of array length or document size.
 * The capture is a compact, self-delimiting event stream (not re-serialized
 * JSON), walked by two passes that mirror json2toon's encode_array.c.
 */

/* Feature-test macros: fseeko/ftello + a 64-bit off_t (a spilled array can
 * exceed 2 GiB) are POSIX, hidden by glibc under a strict standard. Set before
 * any system header. Mirrors json2toon/src/j2t_config.h. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#if !defined(_POSIX_C_SOURCE) && !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <toonwriter.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "toon_numeric.c"

/* ----------------------------------------------- platform: 64-bit seek + temp */

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <share.h>
#  include <sys/stat.h>
#  define TOONW_FSEEK(fp, off) (_fseeki64((fp), (long long)(off), SEEK_SET))
#else
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__unix__) || defined(__APPLE__)
#    define TOONW_FSEEK(fp, off) (fseeko((fp), (off_t)(off), SEEK_SET))
#  else
#    define TOONW_FSEEK(fp, off) (fseek((fp), (long)(off), SEEK_SET))
#  endif
#endif

/* Open a spill temp file by name, failing if it already exists (O_EXCL) -- the
 * predictable-name symlink/TOCTOU defense for a caller-supplied get_temp_filename
 * in a shared directory. Returns a read+write binary FILE* (spilled bytes are
 * read back), or NULL with errno set. tmpfile() is already safe. */
static FILE *toonw_fopen_excl(const char *name) {
#if defined(_WIN32)
  int fd = -1;
  if (_sopen_s(&fd, name, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYRW,
               _S_IREAD | _S_IWRITE) != 0 || fd < 0)
    return NULL;
  {
    FILE *fp = _fdopen(fd, "w+b");
    if (!fp)
      _close(fd);
    return fp;
  }
#else
  FILE *fp;
  int fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return NULL;
  fp = fdopen(fd, "w+b");
  if (!fp)
    close(fd);
  return fp;
#endif
}

#define TOONW_DELIM ','
#define TOONW_MAX_DEPTH 256       /* bounds capture nesting and emit recursion */
#define TOONW_OUT_BUFSZ 65536

/* Captured-array event stream tokens (1 byte each, then varint len + bytes for
 * KEY/STR/RAW). The stream is well-nested: an object is START_OBJ (KEY value)*
 * END_OBJ; an array is START_ARR value* END_ARR; a value is a scalar or a
 * container. STR is a string value (TOON-quoted on emit); RAW is an already
 * emit-ready token (number / true / false / null) written verbatim. */
enum {
  EV_START_OBJ = 1,
  EV_END_OBJ = 2,
  EV_START_ARR = 3,
  EV_END_ARR = 4,
  EV_KEY = 5,
  EV_STR = 6,
  EV_RAW = 7
};

/* Array classification (precedence: EMPTY > INLINE > TABULAR > LIST). */
enum { ARR_EMPTY, ARR_INLINE, ARR_TABULAR, ARR_LIST };

/* ------------------------------------------------------- growable byte buffer */

typedef struct {
  char *p;
  size_t len, cap;
} toonw_buf;

static int toonw_buf_reserve(toonw_buf *b, size_t need) {
  /* Also allocate when p is still NULL (even for need==0): callers compute
   * `b->p + offset`, and `NULL + 0` is undefined behavior. */
  if (b->p && need <= b->cap)
    return 0;
  {
    size_t nc = b->cap ? b->cap : 64;
    char *nb;
    while (nc < need)
      nc *= 2;
    nb = (char *)realloc(b->p, nc);
    if (!nb)
      return -1;
    b->p = nb;
    b->cap = nc;
  }
  return 0;
}

static int toonw_buf_append(toonw_buf *b, const char *s, size_t n) {
  if (toonw_buf_reserve(b, b->len + n) != 0)
    return -1;
  if (n)
    memcpy(b->p + b->len, s, n);
  b->len += n;
  return 0;
}

static void toonw_buf_free(toonw_buf *b) {
  free(b->p);
  b->p = NULL;
  b->len = 0;
  b->cap = 0;
}

/* ----------------------------------------- bounded backing store (RAM + spill) */

/* Append-then-read store for one captured array's event bytes. Bytes live in
 * `ram` up to `threshold`; the overflow spills to a temp file so peak RAM stays
 * bounded regardless of array length. `ram` is reused across captured arrays.
 * Ported from json2toon/src/store.c. */
typedef struct {
  toonw_buf ram;
  size_t threshold;
  int spilled;
  FILE *fp;
  char *tmpname;
  uint64_t total;
  char *(*get_temp_filename)(const char *prefix);
  int err; /* sticky toonwriter_status: out_of_memory (malloc) / io_error (spill) */
} toonw_store;

static void toonw_store_init(toonw_store *s, size_t threshold,
                             char *(*get_temp_filename)(const char *prefix)) {
  s->ram.p = NULL;
  s->ram.len = 0;
  s->ram.cap = 0;
  s->threshold = threshold ? threshold : (1u << 20);
  s->spilled = 0;
  s->fp = NULL;
  s->tmpname = NULL;
  s->total = 0;
  s->get_temp_filename = get_temp_filename;
  s->err = toonwriter_status_ok;
}

/* Move to spilled mode: obtain the temp file and flush the resident bytes into
 * it. After this, `ram` no longer mirrors the data and is free for reuse. */
static int toonw_spill_open(toonw_store *s) {
  if (s->get_temp_filename) {
    s->tmpname = s->get_temp_filename("toonwriter");
    if (!s->tmpname) {
      s->err = toonwriter_status_io_error;
      return -1;
    }
    s->fp = toonw_fopen_excl(s->tmpname);
  } else {
    s->fp = tmpfile();
  }
  if (!s->fp) {
    s->err = toonwriter_status_io_error;
    return -1;
  }
  if (s->ram.len && fwrite(s->ram.p, 1, s->ram.len, s->fp) != s->ram.len) {
    s->err = toonwriter_status_io_error;
    return -1;
  }
  s->spilled = 1;
  s->ram.len = 0;
  return 0;
}

static int toonw_store_append(toonw_store *s, const char *p, size_t n) {
  if (s->err != toonwriter_status_ok)
    return s->err;
  if (n == 0)
    return toonwriter_status_ok;

  if (!s->spilled && s->ram.len + n > s->threshold) {
    if (toonw_spill_open(s) != 0)
      return s->err;
  }

  if (s->spilled) {
    if (fwrite(p, 1, n, s->fp) != n) {
      s->err = toonwriter_status_io_error;
      return s->err;
    }
  } else {
    if (toonw_buf_append(&s->ram, p, n) != 0) {
      s->err = toonwriter_status_out_of_memory;
      return s->err;
    }
  }
  s->total += n;
  return toonwriter_status_ok;
}

/* Close+remove any temp file and ready the store for the next array (keeps ram). */
static void toonw_store_reset(toonw_store *s) {
  if (s->fp) {
    fclose(s->fp);
    s->fp = NULL;
  }
  if (s->tmpname) {
    remove(s->tmpname);
    free(s->tmpname);
    s->tmpname = NULL;
  }
  s->spilled = 0;
  s->ram.len = 0;
  s->total = 0;
  s->err = toonwriter_status_ok;
}

static void toonw_store_free(toonw_store *s) {
  toonw_store_reset(s);
  toonw_buf_free(&s->ram);
}

/* Sequential reader with seek over a store, sourcing from ram or the spill file
 * transparently. Ported from json2toon/src/store.c. */
typedef struct {
  toonw_store *s;
  uint64_t pos;
  char buf[4096];
  uint64_t buf_off;
  size_t buf_len;
} toonw_reader;

static void toonw_reader_init(toonw_reader *r, toonw_store *s) {
  r->s = s;
  r->pos = 0;
  r->buf_off = 0;
  r->buf_len = 0;
}

static void toonw_reader_seek(toonw_reader *r, uint64_t off) { r->pos = off; }
static uint64_t toonw_reader_tell(const toonw_reader *r) { return r->pos; }

static void toonw_reader_skip(toonw_reader *r, uint64_t n) {
  r->pos += n;
  if (r->pos > r->s->total)
    r->pos = r->s->total;
}

/* Make buf[] cover r->pos (spilled stores only). 0 on success, -1 at
 * end-of-data or on an I/O error (recorded in the store). */
static int toonw_reader_fill(toonw_reader *r) {
  toonw_store *s = r->s;
  size_t got;
  if (r->pos >= s->total)
    return -1;
  if (r->pos >= r->buf_off && r->pos < r->buf_off + r->buf_len)
    return 0;
  if (TOONW_FSEEK(s->fp, r->pos) != 0) {
    s->err = toonwriter_status_io_error;
    return -1;
  }
  got = fread(r->buf, 1, sizeof r->buf, s->fp);
  if (got == 0) {
    s->err = toonwriter_status_io_error;
    return -1;
  }
  r->buf_off = r->pos;
  r->buf_len = got;
  return 0;
}

static int toonw_reader_getc(toonw_reader *r) {
  toonw_store *s = r->s;
  int c;
  if (r->pos >= s->total)
    return -1;
  if (!s->spilled) {
    c = (unsigned char)s->ram.p[r->pos];
  } else {
    if (toonw_reader_fill(r) != 0)
      return -1;
    c = (unsigned char)r->buf[r->pos - r->buf_off];
  }
  r->pos++;
  return c;
}

static int toonw_reader_peek(toonw_reader *r) {
  toonw_store *s = r->s;
  if (r->pos >= s->total)
    return -1;
  if (!s->spilled)
    return (unsigned char)s->ram.p[r->pos];
  if (toonw_reader_fill(r) != 0)
    return -1;
  return (unsigned char)r->buf[r->pos - r->buf_off];
}

static size_t toonw_reader_read(toonw_reader *r, char *dst, size_t n) {
  toonw_store *s = r->s;
  uint64_t avail = s->total - r->pos;
  size_t got;
  if (r->pos >= s->total)
    return 0;
  if ((uint64_t)n > avail)
    n = (size_t)avail;
  if (!s->spilled) {
    memcpy(dst, s->ram.p + r->pos, n);
    r->pos += n;
    return n;
  }
  if (TOONW_FSEEK(s->fp, r->pos) != 0) {
    s->err = toonwriter_status_io_error;
    return 0;
  }
  got = fread(dst, 1, n, s->fp);
  if (got != n)
    s->err = toonwriter_status_io_error;
  r->pos += got;
  return got;
}

/* Read a LEB128 varint. 0 on success, -1 on truncation. */
static int toonw_reader_varint(toonw_reader *r, uint64_t *out) {
  uint64_t v = 0;
  int shift = 0, c;
  for (;;) {
    c = toonw_reader_getc(r);
    if (c < 0)
      return -1;
    v |= (uint64_t)(c & 0x7f) << shift;
    if (!(c & 0x80))
      break;
    shift += 7;
    if (shift >= 64)
      return -1;
  }
  *out = v;
  return 0;
}

/* ------------------------------------------------------------- output buffer */

typedef size_t (*toonw_write_fn)(const void *restrict, size_t, size_t,
                                 void *restrict);

typedef struct {
  unsigned char *buf;
  size_t used;
  toonw_write_fn write;
  void *write_arg;
  unsigned indent; /* spaces per indentation level */
  int err; /* sticky: nonzero once the sink reports a short write */
} toonw_out;

static void toonw_out_flush(toonw_out *o) {
  if (o->used) {
    if (!o->err && o->write(o->buf, o->used, 1, o->write_arg) != 1)
      o->err = 1;
    o->used = 0;
  }
}

static void toonw_out_write(toonw_out *o, const unsigned char *s, size_t n) {
  if (o->err || n == 0)
    return;
  if (n + o->used > TOONW_OUT_BUFSZ) {
    toonw_out_flush(o);
    if (n > TOONW_OUT_BUFSZ) { /* too big for the buffer: write straight through */
      if (!o->err && o->write(s, n, 1, o->write_arg) != 1)
        o->err = 1;
      return;
    }
  }
  memcpy(o->buf + o->used, s, n);
  o->used += n;
}

static void toonw_out_putc(toonw_out *o, char c) {
  unsigned char b = (unsigned char)c;
  toonw_out_write(o, &b, 1);
}

static void toonw_out_puts(toonw_out *o, const char *s) {
  toonw_out_write(o, (const unsigned char *)s, strlen(s));
}

static void toonw_out_indent(toonw_out *o, unsigned level) {
  static const char spaces[16] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  size_t n = (size_t)level * o->indent; /* spaces per level (TOON default 2) */
  while (n >= sizeof spaces) {
    toonw_out_write(o, (const unsigned char *)spaces, sizeof spaces);
    n -= sizeof spaces;
  }
  if (n)
    toonw_out_write(o, (const unsigned char *)spaces, n);
}

/* Emit a non-negative count as decimal (the "N" in a TOON "[N]" header). */
static void toonw_emit_count(toonw_out *o, size_t n) {
  char b[24];
  size_t i = sizeof b;
  if (n == 0) {
    toonw_out_putc(o, '0');
    return;
  }
  while (n) {
    b[--i] = (char)('0' + (n % 10));
    n /= 10;
  }
  toonw_out_write(o, (const unsigned char *)b + i, sizeof b - i);
}

/* --------------------------------------------------------- scalar formatting */

static int toonw_str_eq(const char *s, size_t n, const char *lit) {
  return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

/* True if the string value must be quoted in TOON output. */
static int toonw_str_needs_quote(const char *s, size_t n) {
  size_t i;
  if (n == 0)
    return 1; /* empty string */
  if (toonw_str_eq(s, n, "true") || toonw_str_eq(s, n, "false") ||
      toonw_str_eq(s, n, "null"))
    return 1; /* literal collision */
  if (is_valid_toon_number(s, n))
    return 1; /* numeric collision */
  if (s[0] == ' ' || s[0] == '\t' || s[n - 1] == ' ' || s[n - 1] == '\t')
    return 1; /* leading/trailing whitespace */
  if (s[0] == '-')
    return 1; /* hyphen prefix (list marker) */
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20)
      return 1; /* control char */
    if (c == ':' || c == '"' || c == '\\' || c == '[' || c == ']' || c == '{' ||
        c == '}' || c == TOONW_DELIM)
      return 1; /* structural / delimiter */
  }
  return 0;
}

/* True if an object key / tabular field name may be emitted unquoted:
 * ^[A-Za-z_][A-Za-z0-9_]*$  (dotted keys are path-folding; not emitted here). */
static int toonw_key_is_bare(const char *s, size_t n) {
  size_t i;
  if (n == 0)
    return 0;
  if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') ||
        s[0] == '_'))
    return 0;
  for (i = 1; i < n; i++) {
    char c = s[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_'))
      return 0;
  }
  return 1;
}

static void toonw_emit_hex4(toonw_out *o, unsigned v) {
  static const char hex[] = "0123456789abcdef";
  char b[6];
  b[0] = '\\';
  b[1] = 'u';
  b[2] = hex[(v >> 12) & 0xf];
  b[3] = hex[(v >> 8) & 0xf];
  b[4] = hex[(v >> 4) & 0xf];
  b[5] = hex[v & 0xf];
  toonw_out_write(o, (const unsigned char *)b, 6);
}

/* Two-char TOON escapes by byte; NULL => emit verbatim, or \uXXXX when c<0x20. */
static const char *const toonw_escape[256] = {
  ['"'] = "\\\"", ['\\'] = "\\\\",
  ['\n'] = "\\n", ['\r'] = "\\r", ['\t'] = "\\t",
};

static void toonw_emit_quoted(toonw_out *o, const char *s, size_t n) {
  size_t i, run = 0;
  toonw_out_putc(o, '"');
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    const char *esc = toonw_escape[c];
    if (!esc && c >= 0x20) {
      run++;
      continue;
    }
    if (run) {
      toonw_out_write(o, (const unsigned char *)s + i - run, run);
      run = 0;
    }
    if (esc)
      toonw_out_write(o, (const unsigned char *)esc, 2);
    else
      toonw_emit_hex4(o, c); /* control char < 0x20 with no short escape */
  }
  if (run)
    toonw_out_write(o, (const unsigned char *)s + n - run, run);
  toonw_out_putc(o, '"');
}

static void toonw_emit_string(toonw_out *o, const char *s, size_t n) {
  if (toonw_str_needs_quote(s, n))
    toonw_emit_quoted(o, s, n);
  else
    toonw_out_write(o, (const unsigned char *)s, n);
}

static void toonw_emit_key(toonw_out *o, const char *s, size_t n) {
  if (toonw_key_is_bare(s, n))
    toonw_out_write(o, (const unsigned char *)s, n);
  else
    toonw_emit_quoted(o, s, n);
}

/* Emit one scalar: a string gets TOON quoting; a RAW token is verbatim. */
static void toonw_emit_scalar(toonw_out *o, int is_string, const char *v,
                              size_t n) {
  if (is_string)
    toonw_emit_string(o, v, n);
  else
    toonw_out_write(o, (const unsigned char *)v, n);
}

/* ---------------------------------------------------------- the writer handle */

typedef struct {
  size_t off, len;
} toonw_col;
typedef struct {
  size_t koff, klen, voff, vlen;
  int vstr;
} toonw_cell;

struct toonwriter_data {
  toonw_out out;

  /* streaming state (outside any captured array, all open containers are
   * objects -- TOON objects need no count/close-token lookahead). */
  unsigned depth;                      /* number of open containers */
  unsigned char ctype[TOONW_MAX_DEPTH]; /* per level: '{' object or '[' array */
  toonw_buf key;                       /* pending object key awaiting its value */
  int has_key;

  /* options */
  unsigned char compact; /* accepted for API parity; TOON output is canonical */

  /* array capture */
  int capturing;
  unsigned cap_depth; /* nesting within the capture; 0 closes the captured array */
  unsigned cap_level; /* indent level of the captured array's header line */
  int cap_has_key;    /* the captured array is an object member (key in `key`) */
  toonw_store store;

  /* reusable encoder scratch (grown to the widest seen; one captured array uses
   * a fixed handful of buffers regardless of length/nesting) */
  toonw_buf tpl_buf;
  toonw_col *tpl;
  size_t tpl_n, tpl_cap; /* tabular column template */
  toonw_buf kl_buf;
  toonw_col *kl;
  size_t kl_n, kl_cap; /* one object's keys (shape + tabular rows) */
  toonw_buf rv_buf;
  toonw_cell *cells;
  size_t ncells, cellcap; /* one tabular row's cells */
  toonw_buf sv_buf;       /* one scalar value, read back for emit */
  toonw_buf wk_buf;       /* one object member key, read back for emit */

  int err; /* sticky toonwriter_status */

  struct toonwriter_variant (*to_variant)(void *);
  void (*after_variant)(void *, struct toonwriter_variant *);

  char tmp[128]; /* number formatting scratch */
};

/* Collapse a store/reader failure to a public status (the limited enum has no
 * dedicated I/O code; spill failures surface as out_of_memory). */
static int h_store_err(toonwriter_handle e) {
  return e->store.err != toonwriter_status_ok ? e->store.err
                                              : toonwriter_status_invalid_value;
}

/* ============================================================ capture writing */

static int toonw_cap_bytes(toonwriter_handle h, const unsigned char *p,
                           size_t n) {
  if (toonw_store_append(&h->store, (const char *)p, n) != 0) {
    if (!h->err)
      h->err = h->store.err;
    return 1;
  }
  return 0;
}

static int toonw_cap_type(toonwriter_handle h, unsigned char t) {
  return toonw_cap_bytes(h, &t, 1);
}

static int toonw_cap_varint(toonwriter_handle h, uint64_t v) {
  unsigned char tmp[10];
  size_t k = 0;
  do {
    unsigned char b = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (v)
      b |= 0x80;
    tmp[k++] = b;
  } while (v);
  return toonw_cap_bytes(h, tmp, k);
}

/* Record a length-prefixed event (KEY / STR / RAW). */
static int toonw_cap_lenpfx(toonwriter_handle h, unsigned char t, const char *b,
                            size_t n) {
  if (toonw_cap_type(h, t) || toonw_cap_varint(h, (uint64_t)n) ||
      toonw_cap_bytes(h, (const unsigned char *)b, n))
    return 1;
  return 0;
}

/* ============================================================ shape (pass 1) */

static int toonw_col_find_tpl(const toonwriter_handle e, const char *k,
                              size_t kl) {
  size_t i;
  for (i = 0; i < e->tpl_n; i++)
    if (e->tpl[i].len == kl &&
        memcmp(e->tpl_buf.p + e->tpl[i].off, k, kl) == 0)
      return (int)i;
  return -1;
}

/* Append a decoded key to the current object's key list (shape). */
static int toonw_kl_push(toonwriter_handle e, const char *k, size_t kl) {
  size_t off = e->kl_buf.len;
  if (toonw_buf_append(&e->kl_buf, k, kl) != 0)
    return -1;
  if (e->kl_n == e->kl_cap) {
    size_t nc = e->kl_cap ? e->kl_cap * 2 : 8;
    toonw_col *np = (toonw_col *)realloc(e->kl, nc * sizeof *np);
    if (!np)
      return -1;
    e->kl = np;
    e->kl_cap = nc;
  }
  e->kl[e->kl_n].off = off;
  e->kl[e->kl_n].len = kl;
  e->kl_n++;
  return 0;
}

/* Snapshot the current key list as the column template; flag a duplicate key. */
static int toonw_build_tpl(toonwriter_handle e, int *dup) {
  size_t i;
  e->tpl_n = 0;
  e->tpl_buf.len = 0;
  *dup = 0;
  for (i = 0; i < e->kl_n; i++) {
    const char *k = e->kl_buf.p + e->kl[i].off;
    size_t kl = e->kl[i].len;
    size_t off = e->tpl_buf.len;
    if (toonw_col_find_tpl(e, k, kl) >= 0)
      *dup = 1;
    if (toonw_buf_append(&e->tpl_buf, k, kl) != 0)
      return -1;
    if (e->tpl_n == e->tpl_cap) {
      size_t nc = e->tpl_cap ? e->tpl_cap * 2 : 8;
      toonw_col *np = (toonw_col *)realloc(e->tpl, nc * sizeof *np);
      if (!np)
        return -1;
      e->tpl = np;
      e->tpl_cap = nc;
    }
    e->tpl[e->tpl_n].off = off;
    e->tpl[e->tpl_n].len = kl;
    e->tpl_n++;
  }
  return 0;
}

/* True if the current key list is exactly the template's key set (same count,
 * no duplicates, every key present) -- order may differ (TOON spec §9.3). */
static int toonw_kl_matches_tpl(const toonwriter_handle e) {
  size_t i, j;
  if (e->kl_n != e->tpl_n)
    return 0;
  for (i = 0; i < e->kl_n; i++) {
    const char *k = e->kl_buf.p + e->kl[i].off;
    size_t kl = e->kl[i].len;
    for (j = 0; j < i; j++)
      if (e->kl[j].len == kl &&
          memcmp(e->kl_buf.p + e->kl[j].off, k, kl) == 0)
        return 0; /* duplicate within the object */
    if (toonw_col_find_tpl(e, k, kl) < 0)
      return 0;
  }
  return 1;
}

/* Classify the array at [start,end): kind + element count, and (TABULAR) build
 * the column template into e->tpl. Mirrors json2toon encode_array.c shape pass.
 * Returns 0, or -1 with e->err set (OOM or a duplicate key). */
static int toonw_shape(toonwriter_handle e, toonw_reader *r, uint64_t start,
                       uint64_t end, int *kind, size_t *count) {
  int depth = 0;
  size_t cnt = 0;
  int all_scalar = 1, all_objects = 1, tab_ok = 1, have_tpl = 0;

  toonw_reader_seek(r, start);
  while (toonw_reader_tell(r) < end) {
    int t = toonw_reader_getc(r);
    if (t < 0)
      break;
    switch (t) {
    case EV_STR:
    case EV_RAW: {
      uint64_t n;
      if (toonw_reader_varint(r, &n) != 0)
        goto io_err;
      toonw_reader_skip(r, n);
      if (depth == 1) {
        cnt++;
        all_objects = 0;
      }
      break;
    }
    case EV_KEY: {
      uint64_t n;
      if (toonw_reader_varint(r, &n) != 0)
        goto io_err;
      if (depth == 2) { /* a direct-child object's key */
        size_t i, koff = e->kl_buf.len;
        if (toonw_buf_reserve(&e->kl_buf, koff + (size_t)n) != 0)
          goto oom;
        if (toonw_reader_read(r, e->kl_buf.p + koff, (size_t)n) != (size_t)n)
          goto io_err;
        for (i = 0; i < e->kl_n; i++) /* unique-key rule: reject a duplicate */
          if (e->kl[i].len == (size_t)n &&
              memcmp(e->kl_buf.p + e->kl[i].off, e->kl_buf.p + koff,
                     (size_t)n) == 0) {
            e->err = toonwriter_status_invalid_value;
            return -1;
          }
        if (toonw_kl_push(e, e->kl_buf.p + koff, (size_t)n) != 0)
          goto oom;
      } else {
        toonw_reader_skip(r, n);
      }
      break;
    }
    case EV_START_OBJ:
      if (depth == 1) {
        cnt++;
        all_scalar = 0;
        e->kl_n = 0;
        e->kl_buf.len = 0;
      } else if (depth >= 2) {
        tab_ok = 0; /* a container as an object member value */
      }
      depth++;
      break;
    case EV_START_ARR:
      if (depth == 1) {
        cnt++;
        all_scalar = 0;
        all_objects = 0;
        tab_ok = 0;
      } else if (depth >= 2) {
        tab_ok = 0;
      }
      depth++;
      break;
    case EV_END_OBJ:
      depth--;
      if (depth == 1 && tab_ok && all_objects) {
        if (e->kl_n == 0) {
          tab_ok = 0; /* empty object: not tabular */
        } else if (!have_tpl) {
          int dup = 0;
          if (toonw_build_tpl(e, &dup) != 0)
            goto oom;
          if (dup)
            tab_ok = 0;
          have_tpl = 1;
        } else if (!toonw_kl_matches_tpl(e)) {
          tab_ok = 0;
        }
      }
      break;
    case EV_END_ARR:
      depth--;
      if (depth == 0)
        goto done; /* the array itself closed */
      break;
    default:
      goto io_err;
    }
  }
done:
  *count = cnt;
  if (cnt == 0)
    *kind = ARR_EMPTY;
  else if (all_scalar)
    *kind = ARR_INLINE;
  else if (all_objects && tab_ok)
    *kind = ARR_TABULAR;
  else
    *kind = ARR_LIST;
  return 0;
oom:
  e->err = toonwriter_status_out_of_memory;
  return -1;
io_err:
  if (!e->err)
    e->err = h_store_err(e);
  return -1;
}

/* ============================================================ emit (pass 2) */

static void toonw_emit_array_at(toonwriter_handle e, toonw_reader *r,
                                uint64_t start, uint64_t end, unsigned level,
                                const char *empty_str);
static void toonw_emit_object_body(toonwriter_handle e, toonw_reader *r,
                                   uint64_t start, uint64_t end, unsigned level,
                                   int first_inline);
static void toonw_emit_array_dispatch(toonwriter_handle e, toonw_reader *r,
                                      uint64_t start, uint64_t end,
                                      unsigned level, int kind, size_t count);

/* Consume exactly one value (recursively for containers) so the reader lands
 * just past it. Recursion is bounded by nesting depth (<= TOONW_MAX_DEPTH). */
static void toonw_skip_value(toonw_reader *r) {
  int t = toonw_reader_getc(r);
  if (t == EV_STR || t == EV_RAW) {
    uint64_t n;
    if (toonw_reader_varint(r, &n) == 0)
      toonw_reader_skip(r, n);
  } else if (t == EV_START_OBJ) {
    for (;;) {
      int p = toonw_reader_peek(r);
      if (p == EV_END_OBJ || p < 0) {
        toonw_reader_getc(r);
        return;
      }
      toonw_reader_getc(r); /* EV_KEY */
      {
        uint64_t kn;
        if (toonw_reader_varint(r, &kn) != 0)
          return;
        toonw_reader_skip(r, kn);
      }
      toonw_skip_value(r);
    }
  } else if (t == EV_START_ARR) {
    for (;;) {
      int p = toonw_reader_peek(r);
      if (p == EV_END_ARR || p < 0) {
        toonw_reader_getc(r);
        return;
      }
      toonw_skip_value(r);
    }
  }
}

/* Read a varint-prefixed blob into buf at buf->len, advancing buf->len. Returns
 * the blob length, or (size_t)-1 on error (OOM sets e->err). Centralizes the
 * reserve/short-read/OOM teardown shared by every length-prefixed read. */
static size_t toonw_read_blob(toonwriter_handle e, toonw_reader *r,
                              toonw_buf *buf) {
  uint64_t n;
  size_t off = buf->len;
  if (toonw_reader_varint(r, &n) != 0)
    return (size_t)-1;
  if (toonw_buf_reserve(buf, off + (size_t)n) != 0) {
    e->err = toonwriter_status_out_of_memory;
    return (size_t)-1;
  }
  if (toonw_reader_read(r, buf->p + off, (size_t)n) != (size_t)n)
    return (size_t)-1;
  buf->len = off + (size_t)n;
  return (size_t)n;
}

/* Read a scalar (STR/RAW) at the reader into `buf`. Returns 1 if it was a
 * string, 0 if RAW, -1 on error. */
static int toonw_read_scalar(toonwriter_handle e, toonw_reader *r,
                             toonw_buf *buf) {
  int t = toonw_reader_getc(r);
  if (t != EV_STR && t != EV_RAW)
    return -1;
  buf->len = 0;
  if (toonw_read_blob(e, r, buf) == (size_t)-1)
    return -1;
  return t == EV_STR ? 1 : 0;
}

/* ----- inline: "[N]: v1,v2,..." (all elements scalar) ----- */
static void toonw_emit_inline(toonwriter_handle e, toonw_reader *r,
                              uint64_t start, size_t count) {
  size_t idx = 0;
  toonw_reader_seek(r, start);
  toonw_reader_getc(r); /* EV_START_ARR */
  toonw_out_putc(&e->out, '[');
  toonw_emit_count(&e->out, count);
  toonw_out_puts(&e->out, "]: ");
  for (;;) {
    int p = toonw_reader_peek(r), vstr;
    if (p == EV_END_ARR || p < 0) {
      toonw_reader_getc(r);
      break;
    }
    if (idx++)
      toonw_out_putc(&e->out, TOONW_DELIM);
    vstr = toonw_read_scalar(e, r, &e->sv_buf);
    if (vstr < 0)
      return;
    toonw_emit_scalar(&e->out, vstr, e->sv_buf.p, e->sv_buf.len);
  }
  toonw_out_putc(&e->out, '\n');
}

/* ----- tabular: "[N]{cols}:" then one row per object, in column order ----- */
static void toonw_emit_tabular(toonwriter_handle e, toonw_reader *r,
                               uint64_t start, unsigned level, size_t count) {
  size_t t;
  toonw_reader_seek(r, start);
  toonw_reader_getc(r); /* EV_START_ARR */
  toonw_out_putc(&e->out, '[');
  toonw_emit_count(&e->out, count);
  toonw_out_puts(&e->out, "]{");
  for (t = 0; t < e->tpl_n; t++) {
    if (t)
      toonw_out_putc(&e->out, TOONW_DELIM);
    toonw_emit_key(&e->out, e->tpl_buf.p + e->tpl[t].off, e->tpl[t].len);
  }
  toonw_out_puts(&e->out, "}:");
  toonw_out_putc(&e->out, '\n');

  for (;;) {
    int p = toonw_reader_peek(r);
    if (p == EV_END_ARR || p < 0) {
      toonw_reader_getc(r);
      break;
    }
    toonw_reader_getc(r); /* EV_START_OBJ (a row) */
    e->kl_buf.len = 0;
    e->rv_buf.len = 0;
    e->ncells = 0;
    for (;;) {
      int q = toonw_reader_peek(r);
      size_t kn, vn, koff, voff;
      int vt;
      if (q == EV_END_OBJ || q < 0) {
        toonw_reader_getc(r);
        break;
      }
      toonw_reader_getc(r); /* EV_KEY */
      koff = e->kl_buf.len;
      if ((kn = toonw_read_blob(e, r, &e->kl_buf)) == (size_t)-1)
        return;
      vt = toonw_reader_getc(r); /* EV_STR / EV_RAW (shape guaranteed scalar) */
      voff = e->rv_buf.len;
      if ((vn = toonw_read_blob(e, r, &e->rv_buf)) == (size_t)-1)
        return;
      if (e->ncells == e->cellcap) {
        size_t nc = e->cellcap ? e->cellcap * 2 : 8;
        toonw_cell *np = (toonw_cell *)realloc(e->cells, nc * sizeof *np);
        if (!np) {
          e->err = toonwriter_status_out_of_memory;
          return;
        }
        e->cells = np;
        e->cellcap = nc;
      }
      e->cells[e->ncells].koff = koff;
      e->cells[e->ncells].klen = kn;
      e->cells[e->ncells].voff = voff;
      e->cells[e->ncells].vlen = vn;
      e->cells[e->ncells].vstr = (vt == EV_STR);
      e->ncells++;
    }
    /* emit the row in template-column order */
    toonw_out_indent(&e->out, level + 1);
    for (t = 0; t < e->tpl_n; t++) {
      size_t i;
      const char *tk = e->tpl_buf.p + e->tpl[t].off;
      size_t tl = e->tpl[t].len;
      if (t)
        toonw_out_putc(&e->out, TOONW_DELIM);
      for (i = 0; i < e->ncells; i++)
        if (e->cells[i].klen == tl &&
            memcmp(e->kl_buf.p + e->cells[i].koff, tk, tl) == 0) {
          toonw_emit_scalar(&e->out, e->cells[i].vstr,
                            e->rv_buf.p + e->cells[i].voff, e->cells[i].vlen);
          break;
        }
    }
    toonw_out_putc(&e->out, '\n');
  }
}

/* ----- list: "[N]:" then "- item" per element at level+1 ----- */
static void toonw_emit_list(toonwriter_handle e, toonw_reader *r, uint64_t start,
                            unsigned level, size_t count) {
  unsigned il = level + 1;
  toonw_reader_seek(r, start);
  toonw_reader_getc(r); /* EV_START_ARR */
  toonw_out_putc(&e->out, '[');
  toonw_emit_count(&e->out, count);
  toonw_out_puts(&e->out, "]:");
  toonw_out_putc(&e->out, '\n');

  for (;;) {
    int p = toonw_reader_peek(r);
    if (e->err || e->out.err)
      return;
    if (p == EV_END_ARR || p < 0) {
      toonw_reader_getc(r);
      break;
    }
    if (p == EV_STR || p == EV_RAW) {
      int vstr;
      toonw_out_indent(&e->out, il);
      toonw_out_puts(&e->out, "- ");
      vstr = toonw_read_scalar(e, r, &e->sv_buf);
      if (vstr < 0)
        return;
      toonw_emit_scalar(&e->out, vstr, e->sv_buf.p, e->sv_buf.len);
      toonw_out_putc(&e->out, '\n');
    } else if (p == EV_START_OBJ) {
      uint64_t cstart = toonw_reader_tell(r), cend;
      int q;
      toonw_skip_value(r);
      cend = toonw_reader_tell(r);
      toonw_reader_seek(r, cstart);
      toonw_reader_getc(r); /* EV_START_OBJ */
      q = toonw_reader_peek(r);
      toonw_out_indent(&e->out, il);
      if (q == EV_END_OBJ) { /* empty object item: bare "-" */
        toonw_out_putc(&e->out, '-');
        toonw_out_putc(&e->out, '\n');
      } else {
        toonw_out_puts(&e->out, "- ");
        toonw_emit_object_body(e, r, cstart, cend, il + 1, 1);
      }
      toonw_reader_seek(r, cend);
    } else { /* EV_START_ARR */
      uint64_t cstart = toonw_reader_tell(r), cend;
      toonw_skip_value(r);
      cend = toonw_reader_tell(r);
      toonw_out_indent(&e->out, il);
      toonw_out_puts(&e->out, "- ");
      toonw_emit_array_at(e, r, cstart, cend, il + 1, "[]");
      toonw_reader_seek(r, cend);
    }
  }
}

/* ----- object body: "key: value" / "key:" + nested, one member per line ----- */
static void toonw_emit_object_body(toonwriter_handle e, toonw_reader *r,
                                   uint64_t start, uint64_t end, unsigned level,
                                   int first_inline) {
  size_t idx = 0;
  (void)end;
  toonw_reader_seek(r, start);
  toonw_reader_getc(r); /* EV_START_OBJ */
  for (;;) {
    int p = toonw_reader_peek(r), vt;
    if (e->err || e->out.err)
      return;
    if (p == EV_END_OBJ || p < 0) {
      toonw_reader_getc(r);
      break;
    }
    toonw_reader_getc(r); /* EV_KEY */
    e->wk_buf.len = 0;
    if (toonw_read_blob(e, r, &e->wk_buf) == (size_t)-1)
      return;

    if (!(idx == 0 && first_inline))
      toonw_out_indent(&e->out, level);
    toonw_emit_key(&e->out, e->wk_buf.p, e->wk_buf.len);
    idx++;

    vt = toonw_reader_peek(r);
    if (vt == EV_STR || vt == EV_RAW) {
      int vstr;
      toonw_out_puts(&e->out, ": ");
      vstr = toonw_read_scalar(e, r, &e->sv_buf);
      if (vstr < 0)
        return;
      toonw_emit_scalar(&e->out, vstr, e->sv_buf.p, e->sv_buf.len);
      toonw_out_putc(&e->out, '\n');
    } else if (vt == EV_START_OBJ) {
      uint64_t cstart = toonw_reader_tell(r), cend;
      toonw_skip_value(r);
      cend = toonw_reader_tell(r);
      toonw_out_putc(&e->out, ':');
      toonw_out_putc(&e->out, '\n');
      toonw_emit_object_body(e, r, cstart, cend, level + 1, 0);
      toonw_reader_seek(r, cend);
    } else if (vt == EV_START_ARR) {
      uint64_t cstart = toonw_reader_tell(r), cend;
      toonw_skip_value(r);
      cend = toonw_reader_tell(r);
      toonw_emit_array_at(e, r, cstart, cend, level, ": []");
      toonw_reader_seek(r, cend);
    } else {
      return; /* malformed event stream */
    }
  }
}

static void toonw_emit_array_dispatch(toonwriter_handle e, toonw_reader *r,
                                      uint64_t start, uint64_t end,
                                      unsigned level, int kind, size_t count) {
  (void)end;
  if (e->err || e->out.err)
    return;
  switch (kind) {
  case ARR_INLINE:
    toonw_emit_inline(e, r, start, count);
    break;
  case ARR_TABULAR:
    toonw_emit_tabular(e, r, start, level, count);
    break;
  default:
    toonw_emit_list(e, r, start, level, count);
    break;
  }
}

/* Shape then emit an array reached as a child. `empty_str` is printed in place
 * of the "[N]..." header when the array is empty (list item: "[]"; object
 * member: ": []"). */
static void toonw_emit_array_at(toonwriter_handle e, toonw_reader *r,
                                uint64_t start, uint64_t end, unsigned level,
                                const char *empty_str) {
  int kind;
  size_t count;
  if (e->err || e->out.err)
    return;
  if (toonw_shape(e, r, start, end, &kind, &count) != 0)
    return;
  if (count == 0) {
    toonw_out_puts(&e->out, empty_str);
    toonw_out_putc(&e->out, '\n');
  } else {
    toonw_emit_array_dispatch(e, r, start, end, level, kind, count);
  }
}

/* Shape + emit the just-captured array, then release the store. */
static void toonw_capture_complete(toonwriter_handle h) {
  toonw_reader r;
  int kind;
  size_t count;
  toonw_reader_init(&r, &h->store);
  if (toonw_shape(h, &r, 0, h->store.total, &kind, &count) == 0) {
    toonw_out_indent(&h->out, h->cap_level);
    if (count == 0) {
      if (h->cap_has_key) {
        toonw_emit_key(&h->out, h->key.p, h->key.len);
        toonw_out_puts(&h->out, ": []");
      } else {
        toonw_out_puts(&h->out, "[]");
      }
      toonw_out_putc(&h->out, '\n');
    } else {
      if (h->cap_has_key)
        toonw_emit_key(&h->out, h->key.p, h->key.len);
      toonw_emit_array_dispatch(h, &r, 0, h->store.total, h->cap_level, kind,
                                count);
    }
  }
  if (!h->err && h->store.err != toonwriter_status_ok)
    h->err = h->store.err;
  toonw_store_reset(&h->store);
  h->capturing = 0;
  h->has_key = 0;
}

/* ============================================================ streaming emit */

static void toonw_stream_scalar(toonwriter_handle h, int is_string,
                                const char *b, size_t n) {
  unsigned level = h->has_key ? (h->depth ? h->depth - 1 : 0) : 0;
  toonw_out_indent(&h->out, level);
  if (h->has_key) {
    toonw_emit_key(&h->out, h->key.p, h->key.len);
    toonw_out_puts(&h->out, ": ");
  }
  toonw_emit_scalar(&h->out, is_string, b, n);
  toonw_out_putc(&h->out, '\n');
  h->has_key = 0;
}

/* Emit a scalar value: capture it as an event when inside an array, otherwise
 * stream it as a line. `ev` (EV_STR/EV_RAW) selects quoting on emit. */
static int toonw_value(toonwriter_handle h, unsigned char ev, const char *b,
                       size_t n) {
  if (h->err)
    return h->err;
  if (h->capturing)
    return toonw_cap_lenpfx(h, ev, b, n);
  toonw_stream_scalar(h, ev == EV_STR, b, n);
  return h->err;
}

/* ============================================================ public: lifecycle */

void toonwriter_set_option(toonwriter_handle h, enum toonwriter_option opt) {
  h->compact = (unsigned char)(opt == toonwriter_option_compact);
}

static size_t toonw_fwrite2(const void *restrict p, size_t n, size_t size,
                            void *restrict f) {
  return (size_t)fwrite(p, n, size, (FILE *)f);
}

toonwriter_handle toonwriter_new_stream(toonw_write_fn write, void *write_arg,
                                        struct toonwriter_opts *opts) {
  struct toonwriter_data *h;
  if (!write)
    return NULL;
  h = (struct toonwriter_data *)calloc(1, sizeof *h);
  if (!h)
    return NULL;
  h->out.write = write;
  h->out.write_arg = write_arg;
  h->out.indent = (opts && opts->indent) ? opts->indent : 2;
  if (!(h->out.buf = (unsigned char *)malloc(TOONW_OUT_BUFSZ))) {
    free(h);
    return NULL;
  }
  toonw_store_init(&h->store, opts ? opts->max_buffer_size : 0,
                   opts ? opts->get_temp_filename : NULL);
  return h;
}

toonwriter_handle toonwriter_new(FILE *f, struct toonwriter_opts *opts) {
  return toonwriter_new_stream(toonw_fwrite2, f, opts);
}

void toonwriter_flush(toonwriter_handle h) {
  if (h)
    toonw_out_flush(&h->out);
}

enum toonwriter_status toonwriter_error(toonwriter_handle h) {
  return (enum toonwriter_status)h->err;
}

void toonwriter_delete(toonwriter_handle h) {
  if (!h)
    return;
  toonw_out_flush(&h->out);
  free(h->out.buf);
  toonw_buf_free(&h->key);
  toonw_store_free(&h->store);
  toonw_buf_free(&h->tpl_buf);
  free(h->tpl);
  toonw_buf_free(&h->kl_buf);
  free(h->kl);
  toonw_buf_free(&h->rv_buf);
  free(h->cells);
  toonw_buf_free(&h->sv_buf);
  toonw_buf_free(&h->wk_buf);
  free(h);
}

/* ============================================================ public: structure */

/* Push a container during capture: record its open event and track nesting. */
static int toonw_cap_open(toonwriter_handle h, unsigned char marker,
                          unsigned char ev) {
  if (h->depth >= TOONW_MAX_DEPTH) {
    h->err = toonwriter_status_invalid_value;
    return 1;
  }
  h->ctype[h->depth++] = marker;
  h->cap_depth++;
  return toonw_cap_type(h, ev);
}

int toonwriter_start_object(toonwriter_handle h) {
  if (h->err)
    return h->err;
  if (h->capturing)
    return toonw_cap_open(h, '{', EV_START_OBJ);

  /* streaming: emit "key:" header for a member object, then descend */
  if (h->depth >= TOONW_MAX_DEPTH) {
    h->err = toonwriter_status_invalid_value;
    return h->err;
  }
  if (h->has_key) {
    toonw_out_indent(&h->out, h->depth ? h->depth - 1 : 0);
    toonw_emit_key(&h->out, h->key.p, h->key.len);
    toonw_out_putc(&h->out, ':');
    toonw_out_putc(&h->out, '\n');
    h->has_key = 0;
  }
  h->ctype[h->depth++] = '{';
  return h->err;
}

int toonwriter_start_array(toonwriter_handle h) {
  if (h->err)
    return h->err;
  if (h->capturing)
    return toonw_cap_open(h, '[', EV_START_ARR);

  /* streaming: begin capturing this (top-level) array */
  if (h->depth >= TOONW_MAX_DEPTH) {
    h->err = toonwriter_status_invalid_value;
    return h->err;
  }
  h->capturing = 1;
  h->cap_has_key = h->has_key;
  h->has_key = 0; /* key retained in h->key for capture_complete */
  h->cap_level = h->cap_has_key && h->depth ? h->depth - 1 : 0;
  h->cap_depth = 0;
  toonw_store_reset(&h->store);
  h->ctype[h->depth++] = '[';
  if (toonw_cap_type(h, EV_START_ARR))
    return h->err;
  h->cap_depth = 1;
  return h->err;
}

/* Close one container. `expected` is the open marker that must match ('{' / '['
 * / 0 for any). Returns a toonwriter_status. */
static enum toonwriter_status toonw_close(toonwriter_handle h,
                                          unsigned char expected) {
  unsigned char top;
  if (h->err)
    return (enum toonwriter_status)h->err;
  if (h->depth == 0)
    return toonwriter_status_invalid_end;
  top = h->ctype[h->depth - 1];
  if (expected && top != expected)
    return toonwriter_status_invalid_end;

  if (h->capturing) {
    if (toonw_cap_type(h, top == '{' ? EV_END_OBJ : EV_END_ARR))
      return (enum toonwriter_status)h->err;
    h->depth--;
    h->cap_depth--;
    if (h->cap_depth == 0)
      toonw_capture_complete(h);
    return (enum toonwriter_status)h->err;
  }
  /* streaming: TOON objects have no closing token; just pop */
  h->depth--;
  return toonwriter_status_ok;
}

enum toonwriter_status toonwriter_end(toonwriter_handle h) {
  return toonw_close(h, 0);
}
enum toonwriter_status toonwriter_end_array(toonwriter_handle h) {
  return toonw_close(h, '[');
}
enum toonwriter_status toonwriter_end_object(toonwriter_handle h) {
  return toonw_close(h, '{');
}

int toonwriter_end_all(toonwriter_handle h) {
  while (toonwriter_end(h) == toonwriter_status_ok)
    ;
  return 0;
}

int toonwriter_object_keyn(toonwriter_handle h, const char *key,
                           size_t len_or_zero) {
  size_t len;
  if (h->err)
    return h->err;
  len = len_or_zero ? len_or_zero : (key ? strlen(key) : 0);
  if (h->capturing)
    return toonw_cap_lenpfx(h, EV_KEY, key ? key : "", len);
  h->key.len = 0;
  if (toonw_buf_append(&h->key, key ? key : "", len) != 0) {
    h->err = toonwriter_status_out_of_memory;
    return h->err;
  }
  h->has_key = 1;
  return 0;
}

int toonwriter_object_key(toonwriter_handle h, const char *key) {
  return toonwriter_object_keyn(h, key, key ? strlen(key) : 0);
}

/* ============================================================ public: scalars */

int toonwriter_strn(toonwriter_handle h, const unsigned char *s, size_t len) {
  return toonw_value(h, EV_STR, (const char *)s, len);
}

int toonwriter_str(toonwriter_handle h, const unsigned char *s) {
  if (!s)
    return toonwriter_null(h);
  return toonwriter_strn(h, s, TOONW_STRLEN(s));
}

int toonwriter_cstr(toonwriter_handle h, const char *s) {
  return toonwriter_str(h, (const unsigned char *)s);
}

int toonwriter_cstrn(toonwriter_handle h, const char *s, size_t len) {
  return toonwriter_strn(h, (const unsigned char *)s, len);
}

int toonwriter_bool(toonwriter_handle h, unsigned char value) {
  return value ? toonw_value(h, EV_RAW, "true", 4)
               : toonw_value(h, EV_RAW, "false", 5);
}

int toonwriter_null(toonwriter_handle h) {
  return toonw_value(h, EV_RAW, "null", 4);
}

int toonwriter_int(toonwriter_handle h, toonw_int64 i) {
  int len;
  if (h->err)
    return h->err;
  len = snprintf(h->tmp, sizeof h->tmp, TOONW_INT64_PRINTF_FMT, i);
  if (len < 0 || (size_t)len >= sizeof h->tmp)
    return toonw_value(h, EV_STR, "NaN", 3);
  return toonw_value(h, EV_RAW, h->tmp, (size_t)len);
}

int toonwriter_size_t(toonwriter_handle h, size_t sz) {
  int len;
  if (h->err)
    return h->err;
  len = snprintf(h->tmp, sizeof h->tmp, "%zu", sz);
  if (len < 0 || (size_t)len >= sizeof h->tmp)
    return toonw_value(h, EV_STR, "NaN", 3);
  return toonw_value(h, EV_RAW, h->tmp, (size_t)len);
}

int toonwriter_dblf(toonwriter_handle h, long double d,
                    const char *format_string,
                    unsigned char trim_trailing_zeros_after_dec) {
  int len;
  if (h->err)
    return h->err;
  if (isnan(d))
    return toonw_value(h, EV_RAW, "\"NaN\"", 5);
  if (isinf(d))
    return toonw_value(h, EV_RAW, d < 0 ? "\"-Infinity\"" : "\"Infinity\"",
                       d < 0 ? 11 : 10);
  format_string = format_string ? format_string : "%.15Lf";
  len = snprintf(h->tmp, sizeof h->tmp, format_string, d);
  if (len < 0 || (size_t)len >= sizeof h->tmp)
    return toonw_value(h, EV_RAW, "0", 1);
  if (trim_trailing_zeros_after_dec && memchr(h->tmp, '.', (size_t)len)) {
    while (len && h->tmp[len - 1] == '0')
      len--;
    if (len && h->tmp[len - 1] == '.')
      len--;
    if (!len) {
      h->tmp[0] = '0';
      len = 1;
    }
  }
  return toonw_value(h, EV_RAW, h->tmp, (size_t)len);
}

int toonwriter_dbl(toonwriter_handle h, long double d) {
  return toonwriter_dblf(h, d, NULL, 1);
}

size_t toonwriter_write_raw(toonwriter_handle h, const unsigned char *s,
                            size_t len) {
  /* Caller guarantees a single valid TOON scalar token; emit it verbatim. */
  toonw_value(h, EV_RAW, (const char *)s, len);
  return len;
}

int toonwriter_unknown(toonwriter_handle h, const unsigned char *s, size_t len,
                       toonw_uint32 flags) {
  (void)flags; /* reserved */
  if (is_valid_toon_number((const char *)s, len) ||
      (len == 4 && !memcmp(s, "true", len)) ||
      (len == 5 && !memcmp(s, "false", len)))
    return toonw_value(h, EV_RAW, (const char *)s, len);
  return toonwriter_strn(h, s, len);
}

/* ============================================================ public: variant */

enum toonwriter_status
toonwriter_set_variant_handler(toonwriter_handle h,
                               struct toonwriter_variant (*to_toonw_variant)(void *),
                               void (*cleanup)(void *,
                                               struct toonwriter_variant *)) {
  h->after_variant = cleanup;
  if (!(h->to_variant = to_toonw_variant))
    return toonwriter_status_invalid_value;
  return toonwriter_status_ok;
}

enum toonwriter_status toonwriter_variant(toonwriter_handle h, void *data) {
  struct toonwriter_variant jv;
  int rc = toonwriter_status_unrecognized_variant_type;
  if (!h->to_variant)
    return toonwriter_status_misconfiguration;
  jv = h->to_variant(data);
  switch (jv.type) {
  case toonwriter_datatype_null:
    rc = toonwriter_null(h);
    break;
  case toonwriter_datatype_string:
    rc = toonwriter_str(h, jv.value.str);
    break;
  case toonwriter_datatype_integer:
    rc = toonwriter_int(h, (toonw_int64)jv.value.i);
    break;
  case toonwriter_datatype_float:
    rc = toonwriter_dbl(h, jv.value.dbl);
    break;
  case toonwriter_datatype_bool:
    rc = toonwriter_bool(h, jv.value.b ? 1 : 0);
    break;
  case toonwriter_datatype_raw:
    if (jv.value.str)
      rc = (int)toonwriter_write_raw(h, jv.value.str,
                                     TOONW_STRLEN(jv.value.str));
    else
      rc = toonwriter_null(h);
    break;
  }
  if (h->after_variant)
    h->after_variant(data, &jv);
  return (enum toonwriter_status)rc;
}
