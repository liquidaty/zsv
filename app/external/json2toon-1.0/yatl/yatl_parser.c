/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * The TOON pushdown parser. See yatl_parser.h for the model. Emission is to the
 * client callbacks: this file decides *what* events occur; the client decides
 * what to do with them. A tabular array `users[2]{id,name}:` is reported as an
 * array of objects, exactly as the equivalent JSON would be.
 */
#include "yatl_parser.h"
#include "yatl_alloc.h"
#include "yatl_encode.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>

#define U_UNSET ((unsigned)-1)
#define CB(h) ((h)->callbacks)

/* --------------------------------------------------------------- utilities */

static void set_err(yatl_handle h, const char *msg, size_t off) {
    if (h->status == yatl_status_ok) {
        h->status = yatl_status_error;
        h->parseError = msg;
        h->err_off = off;
    }
    h->state = ST_FAIL;
}

static void set_cancel(yatl_handle h) {
    if (h->status == yatl_status_ok) {
        h->status = yatl_status_client_canceled;
        h->parseError = "client canceled parse via callback return value";
        h->err_off = h->line_off;
    }
    h->state = ST_CANCELED;
}

/* Invoke a client callback expression `call`; a zero return cancels. */
#define EMIT(h, call)                                                   \
    do { if ((call) == 0) { set_cancel(h); return -1; } } while (0)

static int is_key_start(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_key_char(unsigned char c) {
    return is_key_start(c) || (c >= '0' && c <= '9') || c == '.';
}

static int lit_eq(const char *s, size_t n, const char *lit) {
    return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

/* True if a bare (unquoted) token would have to be quoted per spec §7.2 (so in
 * strict mode it is not valid TOON). Keyword/number tokens are handled before
 * this is reached. `delim` is the active delimiter. */
static int needs_quote(const char *s, size_t n, unsigned char delim) {
    size_t i;
    if (n == 0)
        return 1;
    if (s[0] == ' ' || s[0] == '\t' || s[n - 1] == ' ' || s[n - 1] == '\t')
        return 1;
    if (s[0] == '-')
        return 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == (unsigned char)delim)
            return 1;
        if (c == '"' || c == '\\' || c == ':' ||
            c == '[' || c == ']' || c == '{' || c == '}' || c < 0x20)
            return 1;
    }
    return 0;
}

/* Skip a TOON-quoted token. `p` points at the opening '"'. Returns the byte
 * just past the closing '"', or NULL if unterminated. */
static const char *skip_quoted(const char *p, const char *end) {
    p++;
    while (p < end) {
        char c = *p;
        if (c == '\\') {
            p++;
            if (p < end)
                p++;
            continue;
        }
        if (c == '"')
            return p + 1;
        p++;
    }
    return NULL;
}

/* Scan a key/column token at p. Quoted: *ke is set just past the closing quote,
 * or -1 is returned (unterminated). Bare: consumes the is_key_char run, which
 * may be empty -- callers enforce their own is_key_start / non-empty policy. */
static int scan_token(const char *p, const char *end, const char **ke) {
    if (p < end && *p == '"') {
        const char *q = skip_quoted(p, end);
        if (!q)
            return -1;
        *ke = q;
    } else {
        const char *q = p;
        while (q < end && is_key_char((unsigned char)*q)) q++;
        *ke = q;
    }
    return 0;
}

/* True if the token ending at ke (within [.,end)) heads an object member, i.e.
 * is immediately followed by ':' or '['. */
static int looks_like_key(const char *ke, const char *end) {
    return ke < end && (*ke == ':' || *ke == '[');
}

/* Scan one delimiter-separated field at p: a quoted token (skip_quoted) or a
 * bare run up to `delim`/end. Sets *fe just past the field. Returns 0, or -1 on
 * an unterminated quote (error set). */
static int scan_field(yatl_handle h, const char *p, const char *end,
                      unsigned char delim, const char **fe) {
    if (p < end && *p == '"') {
        const char *q = skip_quoted(p, end);
        if (!q) { set_err(h, "unterminated quoted string", h->line_off); return -1; }
        *fe = q;
    } else {
        const char *q = p;
        while (q < end && *q != (char)delim) q++;
        *fe = q;
    }
    return 0;
}

/* Classify a numeric token: 0 not a number, 1 integer, 2 floating point.
 * Validates the JSON number grammar (no leading zeros). */
static int number_kind(const char *s, size_t n) {
    size_t i = 0;
    int dbl = 0;
    if (n == 0)
        return 0;
    if (s[i] == '-' && ++i == n)
        return 0;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    } else {
        return 0;
    }
    if (i < n && s[i] == '.') {
        dbl = 1;
        if (++i == n || s[i] < '0' || s[i] > '9')
            return 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        dbl = 1;
        if (++i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i == n || s[i] < '0' || s[i] > '9')
            return 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i != n)
        return 0;
    return dbl ? 2 : 1;
}

/* strtoll-equivalent unaffected by LOCALE; sets *overflow on range error. The
 * magnitude accumulates unsigned so the whole two's-complement range is
 * representable -- notably LLONG_MIN, whose magnitude exceeds LLONG_MAX. */
static long long parse_integer(const char *s, size_t n, int *overflow) {
    unsigned long long mag = 0, limit;
    int neg = 0;
    size_t i = 0;
    *overflow = 0;
    if (i < n && s[i] == '-') { neg = 1; i++; }
    limit = (unsigned long long)LLONG_MAX + (neg ? 1u : 0u);   /* |LLONG_MIN| when neg */
    for (; i < n; i++) {
        unsigned d = (unsigned)(s[i] - '0');
        if (mag > (limit - d) / 10) {
            *overflow = 1;
            return neg ? LLONG_MIN : LLONG_MAX;
        }
        mag = mag * 10 + d;
    }
    if (neg)
        return mag == (unsigned long long)LLONG_MAX + 1u ? LLONG_MIN : -(long long)mag;
    return (long long)mag;
}

/* ----------------------------------------------------------- decode quoted */

static int scratch_put(yatl_handle h, const char *p, size_t n) {
    return yatl_buf_append(&h->scratch, p, n);
}

/* Decode a TOON-quoted token [s,e) (including the surrounding quotes) into
 * h->scratch. Returns 0 on success, -1 on malformed escape, -2 on OOM. */
static int decode_quoted(yatl_handle h, const char *s, const char *e) {
    const char *p = s + 1;
    const char *q = e - 1;
    yatl_buf_clear(&h->scratch);
    while (p < q) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            int out;
            if (++p >= q)
                return -1;
            if (*p == 'u') {
                unsigned cp;
                char enc[4];
                int el;
                if (p + 5 > q || yatl_hex4(p + 1, &cp) != 0)
                    return -1;
                p += 5;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    unsigned lo;
                    if (p + 6 > q || p[0] != '\\' || p[1] != 'u' ||
                        yatl_hex4(p + 2, &lo) != 0 || lo < 0xdc00 || lo > 0xdfff)
                        return -1;
                    cp = 0x10000u + (((cp - 0xd800u) << 10) | (lo - 0xdc00u));
                    p += 6;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    return -1;              /* lone low surrogate */
                }
                el = yatl_utf8_encode(cp, enc);
                if (scratch_put(h, enc, (size_t)el) != 0)
                    return -2;
                continue;
            }
            out = yatl_unescape((unsigned char)*p);
            if (out < 0)
                return -1;
            if (yatl_buf_putc(&h->scratch, (unsigned char)out) != 0)
                return -2;
            p++;
        } else if (c < 0x20) {
            return -1;                       /* unescaped control byte */
        } else {
            if (yatl_buf_putc(&h->scratch, c) != 0)
                return -2;
            p++;
        }
    }
    return 0;
}

/* --------------------------------------------------------- event emitters */

/* Validate [s,n) as UTF-8 unless the option is off; raise `bad` on failure.
 * Returns 0 if the bytes may be emitted, -1 if a parse error was set. */
static int validate_str(yatl_handle h, const unsigned char *s, size_t n,
                        const char *bad) {
    if (!(h->flags & yatl_dont_validate_strings) && !yatl_validate_utf8(s, n)) {
        set_err(h, bad, h->line_off);
        return -1;
    }
    return 0;
}

static int emit_string(yatl_handle h, const unsigned char *s, size_t n) {
    if (validate_str(h, s, n, "invalid UTF-8 in string") != 0)
        return -1;
    if (CB(h) && CB(h)->yatl_string)
        EMIT(h, CB(h)->yatl_string(h->ctx, n ? s : (const unsigned char *)"", n));
    return 0;
}

static int emit_key_bytes(yatl_handle h, const unsigned char *s, size_t n) {
    if (validate_str(h, s, n, "invalid UTF-8 in key") != 0)
        return -1;
    if (CB(h) && CB(h)->yatl_map_key)
        EMIT(h, CB(h)->yatl_map_key(h->ctx, n ? s : (const unsigned char *)"", n));
    return 0;
}

static int emit_null(yatl_handle h) {
    if (CB(h) && CB(h)->yatl_null)
        EMIT(h, CB(h)->yatl_null(h->ctx));
    return 0;
}

static int emit_bool(yatl_handle h, int b) {
    if (CB(h) && CB(h)->yatl_boolean)
        EMIT(h, CB(h)->yatl_boolean(h->ctx, b));
    return 0;
}

static int emit_start_map(yatl_handle h) {
    if (CB(h) && CB(h)->yatl_start_map)
        EMIT(h, CB(h)->yatl_start_map(h->ctx));
    return 0;
}

static int emit_end_map(yatl_handle h) {
    if (CB(h) && CB(h)->yatl_end_map)
        EMIT(h, CB(h)->yatl_end_map(h->ctx));
    return 0;
}

static int emit_start_array(yatl_handle h) {
    if (CB(h) && CB(h)->yatl_start_array)
        EMIT(h, CB(h)->yatl_start_array(h->ctx));
    return 0;
}

static int emit_end_array(yatl_handle h) {
    if (CB(h) && CB(h)->yatl_end_array)
        EMIT(h, CB(h)->yatl_end_array(h->ctx));
    return 0;
}

/* Report an out-of-range number [s,n): hand it to yatl_error if present, else
 * raise `msg` as a parse error. Returns 0 if the parse may continue, -1 if it
 * must stop (error raised or client canceled). Caller guarantees CB(h). */
static int emit_range_error(yatl_handle h, const char *s, size_t n, const char *msg) {
    if (CB(h)->yatl_error)
        EMIT(h, CB(h)->yatl_error(h->ctx, (const unsigned char *)s, n, ERANGE));
    else { set_err(h, msg, h->line_off); return -1; }
    return 0;
}

/* Parse [s,n) as a double and emit yatl_double; a magnitude that overflows to
 * +/-inf is routed through emit_range_error. Caller guarantees CB(h)->yatl_double.
 * Returns 0, or -1 if the parse must stop. */
static int emit_double_text(yatl_handle h, const char *s, size_t n) {
    double d;
    char *endp;
    yatl_buf_clear(&h->scratch);
    if (yatl_buf_append(&h->scratch, s, n) != 0 || yatl_buf_putc(&h->scratch, 0) != 0) {
        set_err(h, "out of memory", h->line_off);
        return -1;
    }
    errno = 0;
    d = strtod((const char *)h->scratch.data, &endp);
    if ((d == HUGE_VAL || d == -HUGE_VAL) && errno == ERANGE)
        return emit_range_error(h, s, n, "numeric (floating point) overflow");
    EMIT(h, CB(h)->yatl_double(h->ctx, d));
    return 0;
}

/* Emit a number token. yatl_number (verbatim) wins if set. Otherwise dispatch by
 * `kind`: an integer within long long goes to yatl_integer; one that overflows
 * it degrades yatl_error hook -> widen to yatl_double -> parse error, so a valid
 * but large integer is never a hard parse failure when a double sink exists.
 * Doubles go to yatl_double, with +/-inf overflow routed to yatl_error or
 * surfaced as a parse error. */
static int emit_number(yatl_handle h, const char *s, size_t n, int kind) {
    if (!CB(h))
        return 0;
    if (CB(h)->yatl_number) {
        EMIT(h, CB(h)->yatl_number(h->ctx, s, n));
        return 0;
    }
    if (kind == 1) {
        if (CB(h)->yatl_integer) {
            int ov;
            long long v = parse_integer(s, n, &ov);
            if (!ov)
                EMIT(h, CB(h)->yatl_integer(h->ctx, v));
            else if (!CB(h)->yatl_error && CB(h)->yatl_double) {
                if (emit_double_text(h, s, n) != 0) return -1;  /* widen: keep the value */
            } else if (emit_range_error(h, s, n, "integer overflow") != 0)
                return -1;
        }
    } else {
        if (CB(h)->yatl_double && emit_double_text(h, s, n) != 0)
            return -1;
    }
    return 0;
}

/* Emit a TOON scalar token [s,s+n): quoted string, keyword, number, or bare
 * string. `delim` is the active delimiter for the strict bare-string check. */
static int emit_scalar(yatl_handle h, const char *s, size_t n, unsigned char delim) {
    int kind;
    if (n >= 1 && s[0] == '"') {
        int rc;
        if (n < 2 || s[n - 1] != '"' || skip_quoted(s, s + n) != s + n) {
            set_err(h, "malformed quoted string", h->line_off);
            return -1;
        }
        rc = decode_quoted(h, s, s + n);
        if (rc == -2) { set_err(h, "out of memory", h->line_off); return -1; }
        if (rc != 0) { set_err(h, "malformed string escape", h->line_off); return -1; }
        return emit_string(h, h->scratch.data, h->scratch.used);
    }
    if (n == 0)
        return emit_string(h, (const unsigned char *)"", 0);
    if (lit_eq(s, n, "null"))
        return emit_null(h);
    if (lit_eq(s, n, "true"))
        return emit_bool(h, 1);
    if (lit_eq(s, n, "false"))
        return emit_bool(h, 0);
    kind = number_kind(s, n);
    if (kind)
        return emit_number(h, s, n, kind);
    if (!(h->flags & yatl_lenient_scalars) && needs_quote(s, n, delim)) {
        set_err(h, "unquoted value requires quoting", h->line_off);
        return -1;
    }
    return emit_string(h, (const unsigned char *)s, n);
}

/* ----------------------------------------------------------------- frames */

static yatl_frame *push_frame(yatl_handle h, int kind, unsigned header_indent) {
    yatl_frame *f;
    if (h->nframes >= h->max_depth) {
        set_err(h, "maximum nesting depth exceeded", h->line_off);
        return NULL;
    }
    if (h->nframes == h->framecap) {
        size_t nc = h->framecap ? h->framecap * 2 : 16;
        yatl_frame *nf = (yatl_frame *)YA_REALLOC(&h->alloc, h->frames, nc * sizeof *nf);
        if (!nf) {
            set_err(h, "out of memory", h->line_off);
            return NULL;
        }
        h->frames = nf;
        h->framecap = nc;
    }
    f = &h->frames[h->nframes++];
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->header_indent = header_indent;
    f->child_indent = U_UNSET;
    f->delim = ',';
    return f;
}

/* Push a frame and emit its container-start together (object / list-array).
 * On any failure the sticky error is set, NULL returned, and the frame popped
 * so the parse stack and emitted events stay in lockstep. */
static yatl_frame *open_frame(yatl_handle h, int kind, unsigned header_indent) {
    yatl_frame *f = push_frame(h, kind, header_indent);
    if (!f)
        return NULL;
    if ((kind == FR_OBJ ? emit_start_map(h) : emit_start_array(h)) != 0) {
        h->nframes--;
        return NULL;
    }
    return f;
}

static void frame_free_cols(yatl_handle h, yatl_frame *f) {
    size_t i;
    if (f->cols) {
        for (i = 0; i < f->ncols; i++)
            YA_FREE(&h->alloc, f->cols[i]);
        YA_FREE(&h->alloc, f->cols);
        YA_FREE(&h->alloc, f->collen);
    }
    f->cols = NULL;
    f->collen = NULL;
    f->ncols = 0;
}

/* Close the container of a frame being popped: validate any declared count,
 * then emit the matching end event. */
static int close_frame(yatl_handle h, yatl_frame *f) {
    int is_obj = (f->kind == FR_OBJ);
    if (!is_obj) {
        if (!(h->flags & yatl_dont_validate_length) &&
            f->has_expected && f->emitted != f->expected) {
            frame_free_cols(h, f);
            set_err(h, "array length does not match declared count", h->line_off);
            return -1;
        }
        frame_free_cols(h, f);
    }
    return is_obj ? emit_end_map(h) : emit_end_array(h);
}

/* --------------------------------------------------------- array headers */

/* Parse a count after '['. *pp points just past '['. Advances past the digits;
 * missing digits is allowed (sets *has = 0). Saturates rather than wraps. */
static void parse_count(const char **pp, const char *end, size_t *val, int *has) {
    const char *p = *pp;
    size_t v = 0;
    int any = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        size_t d = (size_t)(*p - '0');
        if (v > ((size_t)-1 - d) / 10) v = (size_t)-1;
        else v = v * 10 + d;
        p++;
        any = 1;
    }
    *val = v;
    *has = any;
    *pp = p;
}

/* Split delimiter-separated scalar elements in [p,end) and emit them as array
 * body. Returns element count, or (size_t)-1 on error. */
static size_t emit_inline_elements(yatl_handle h, const char *p, const char *end,
                                   unsigned char delim) {
    size_t count = 0;
    while (p < end) {
        const char *elem = p, *fe;
        if (scan_field(h, p, end, delim, &fe) != 0)
            return (size_t)-1;
        if (emit_scalar(h, elem, (size_t)(fe - elem), delim) != 0)
            return (size_t)-1;
        p = fe;
        if (p < end) {                          /* must be the field separator */
            if (*p != (char)delim) { set_err(h, "expected delimiter", h->line_off); return (size_t)-1; }
            p++;
        }
        count++;
    }
    return count;
}

/* Duplicate a key token [ks,ke) (quoted or bare) into freshly allocated
 * storage, decoding escapes for quoted tokens. On failure returns NULL and sets
 * *bad to 1 for a malformed token or 0 for OOM. */
static unsigned char *dup_token(yatl_handle h, const char *ks, const char *ke,
                                size_t *outlen, int *bad) {
    unsigned char *r;
    *bad = 0;
    if (ks < ke && *ks == '"') {
        int rc = decode_quoted(h, ks, ke);
        if (rc == -2) return NULL;
        if (rc != 0) { *bad = 1; return NULL; }
        r = (unsigned char *)YA_MALLOC(&h->alloc, h->scratch.used ? h->scratch.used : 1);
        if (!r) return NULL;
        memcpy(r, h->scratch.data, h->scratch.used);
        *outlen = h->scratch.used;
    } else {
        size_t n = (size_t)(ke - ks);
        r = (unsigned char *)YA_MALLOC(&h->alloc, n ? n : 1);
        if (!r) return NULL;
        memcpy(r, ks, n);
        *outlen = n;
    }
    return r;
}

/* Parse a TOON array header beginning at [p,end) where *p == '['. Forms: empty
 * ("[]"), inline ("[N]: a,b"), tabular ("[N]{cols}:") or list body ("[N]:").
 * Tabular/list push a frame at `header_indent`. Returns 0 on success. */
static int emit_array_header(yatl_handle h, const char *p, const char *end,
                             unsigned header_indent) {
    size_t expected;
    int has_count;
    unsigned char delim = ',';
    p++;                                        /* '[' */
    if (p < end && *p == ']') {                 /* empty array */
        p++;
        if (p != end) { set_err(h, "trailing text after []", h->line_off); return -1; }
        if (emit_start_array(h) != 0) return -1;
        return emit_end_array(h);
    }
    parse_count(&p, end, &expected, &has_count);
    if (p < end && (*p == '\t' || *p == '|')) { delim = (unsigned char)*p; p++; }
    if (p >= end || *p != ']') { set_err(h, "expected ']' in array header", h->line_off); return -1; }
    p++;                                        /* ']' */

    if (p < end && *p == '{') {                 /* tabular header */
        yatl_frame *f;
        unsigned char **cols = NULL;
        size_t *collen = NULL;
        size_t ncols = 0, idx, built = 0;
        const char *err_msg = "malformed tabular header";
        const char *scan;
        p++;                                    /* '{' */

        /* pass 1: count columns, validate the header ends with ":" at EOL */
        scan = p;
        for (;;) {
            const char *ks = scan, *ke;
            if (scan_token(scan, end, &ke) != 0 || ke == ks)
                goto fail;                      /* unterminated quote or empty name */
            ncols++;
            scan = ke;
            if (scan < end && *scan == (char)delim) { scan++; continue; }
            if (scan < end && *scan == '}') { scan++; break; }
            goto fail;
        }
        if (scan >= end || *scan != ':' || scan + 1 != end)
            goto fail;

        /* pass 2: decode each column name into owned storage */
        cols = (unsigned char **)YA_MALLOC(&h->alloc, ncols * sizeof *cols);
        collen = (size_t *)YA_MALLOC(&h->alloc, ncols * sizeof *collen);
        if (!cols || !collen) { err_msg = "out of memory"; goto fail; }
        scan = p;
        for (idx = 0; idx < ncols; idx++) {
            const char *ks = scan, *ke;
            int bad;
            unsigned char *col;
            (void)scan_token(scan, end, &ke);   /* shape validated in pass 1 */
            col = dup_token(h, ks, ke, &collen[idx], &bad);
            if (!col) { err_msg = bad ? "malformed column name" : "out of memory"; goto fail; }
            cols[idx] = col;
            built = idx + 1;
            scan = ke;
            if (scan < end && *scan == (char)delim) scan++;
        }

        f = push_frame(h, FR_ARR_TAB, header_indent);
        if (!f) goto fail_pushed;              /* depth/OOM already recorded */
        f->expected = expected;
        f->has_expected = has_count;
        f->delim = delim;
        f->cols = cols;
        f->collen = collen;
        f->ncols = ncols;
        if (emit_start_array(h) != 0)
            return -1;                          /* cols now owned by the frame */
        return 0;

    fail:
        set_err(h, err_msg, h->line_off);
    fail_pushed:
        while (built) YA_FREE(&h->alloc, cols[--built]);
        YA_FREE(&h->alloc, cols);
        YA_FREE(&h->alloc, collen);
        return -1;
    }

    if (p < end && *p == ':') {                 /* list body or inline */
        p++;
        if (p == end) {                         /* list-array body */
            yatl_frame *f = open_frame(h, FR_ARR_LIST, header_indent);
            if (!f) return -1;
            f->expected = expected;
            f->has_expected = has_count;
            f->delim = delim;
            return 0;
        }
        if (*p == ' ') p++;
        if (emit_start_array(h) != 0) return -1;
        {
            size_t n = emit_inline_elements(h, p, end, delim);
            if (n == (size_t)-1) return -1;
            if (emit_end_array(h) != 0) return -1;
            if (!(h->flags & yatl_dont_validate_length) && has_count && n != expected) {
                set_err(h, "array length does not match declared count", h->line_off);
                return -1;
            }
        }
        return 0;
    }

    set_err(h, "expected ':' or '{' in array header", h->line_off);
    return -1;
}

/* ------------------------------------------------ members / items / rows */

/* Handle an object member: frame `fi` is FR_OBJ; [p,end) begins at the key;
 * `member_indent` is the key's column (the object's child indent). */
static int handle_member(yatl_handle h, size_t fi, const char *p,
                         const char *end, unsigned member_indent) {
    yatl_frame *f = &h->frames[fi];
    const char *ks = p, *ke;
    char c;

    if (f->kind != FR_OBJ) { set_err(h, "unexpected key in array", h->line_off); return -1; }

    f->emitted++;
    f->child_indent = member_indent;

    if (p < end && *p == '"') {
        if (scan_token(p, end, &ke) != 0) { set_err(h, "unterminated quoted key", h->line_off); return -1; }
    } else {
        if (p >= end || !is_key_start((unsigned char)*p)) {
            set_err(h, "expected object key", h->line_off);
            return -1;
        }
        (void)scan_token(p, end, &ke);
    }

    /* decode + emit the key */
    if (ks < ke && *ks == '"') {
        int rc = decode_quoted(h, ks, ke);
        if (rc == -2) { set_err(h, "out of memory", h->line_off); return -1; }
        if (rc != 0) { set_err(h, "malformed key escape", h->line_off); return -1; }
        if (emit_key_bytes(h, h->scratch.data, h->scratch.used) != 0) return -1;
    } else {
        if (emit_key_bytes(h, (const unsigned char *)ks, (size_t)(ke - ks)) != 0) return -1;
    }
    p = ke;

    if (p >= end) { set_err(h, "expected ':' after key", h->line_off); return -1; }
    c = *p;
    if (c == '[')
        return emit_array_header(h, p, end, member_indent);

    if (c == ':') {
        p++;
        if (p == end)                           /* nested object */
            return open_frame(h, FR_OBJ, member_indent) ? 0 : -1;
        if (*p == ' ') p++;
        if (p == end) { set_err(h, "missing value after ':'", h->line_off); return -1; }
        if ((size_t)(end - p) == 2 && p[0] == '[' && p[1] == ']') {
            if (emit_start_array(h) != 0) return -1;   /* empty array value */
            return emit_end_array(h);
        }
        return emit_scalar(h, p, (size_t)(end - p), ',');
    }

    set_err(h, "expected ':' after key", h->line_off);
    return -1;
}

/* Handle a list-array element: frame `fi` is FR_ARR_LIST; [p,end) is the text
 * after "- "; `dash_indent` is the indent of the '-'. */
static int handle_item(yatl_handle h, size_t fi, const char *p, const char *end,
                       unsigned dash_indent) {
    yatl_frame *f = &h->frames[fi];
    const char *ks, *ke;
    unsigned char delim = f->delim;

    f->emitted++;
    f->child_indent = dash_indent;

    if (p == end) {                             /* "-" alone -> empty object */
        if (emit_start_map(h) != 0) return -1;
        return emit_end_map(h);
    }
    if (*p == '[')
        return emit_array_header(h, p, end, dash_indent);

    /* object item iff the first token is a key immediately followed by ':' or '[' */
    ks = p;
    if (*p == '"') {
        if (scan_token(p, end, &ke) != 0) { set_err(h, "unterminated quoted string", h->line_off); return -1; }
    } else if (is_key_start((unsigned char)*p)) {
        (void)scan_token(p, end, &ke);
    } else {
        ke = p;
    }
    if (ke > ks && looks_like_key(ke, end)) {
        size_t oi;
        if (!open_frame(h, FR_OBJ, dash_indent)) return -1;
        oi = h->nframes - 1;
        if (handle_member(h, oi, p, end, dash_indent + 2) != 0) return -1;
        h->frames[oi].child_indent = dash_indent + 2;
        return 0;
    }
    return emit_scalar(h, p, (size_t)(end - p), delim);
}

/* Handle a tabular row: frame `fi` is FR_ARR_TAB; [p,end) is the full row;
 * `row_indent` is its column. Each row is one object. */
static int handle_row(yatl_handle h, size_t fi, const char *p, const char *end,
                      unsigned row_indent) {
    yatl_frame *f = &h->frames[fi];
    size_t col = 0;
    unsigned char delim = f->delim;
    /* cols/ncols are read before any emit (emit cannot realloc the frame array
     * in a way that invalidates them, but copy the essentials defensively). */
    unsigned char **cols = f->cols;
    size_t *collen = f->collen;
    size_t ncols = f->ncols;

    f->emitted++;
    f->child_indent = row_indent;

    if (emit_start_map(h) != 0) return -1;
    while (col < ncols) {
        const char *cs = p, *fe;
        if (emit_key_bytes(h, cols[col], collen[col]) != 0) return -1;
        if (scan_field(h, p, end, delim, &fe) != 0) return -1;
        if (emit_scalar(h, cs, (size_t)(fe - cs), delim) != 0) return -1;
        p = fe;
        col++;
        if (col < ncols) {
            if (p >= end || *p != (char)delim) { set_err(h, "too few columns in row", h->line_off); return -1; }
            p++;
        }
    }
    if (p != end) { set_err(h, "too many columns in row", h->line_off); return -1; }
    return emit_end_map(h);
}

/* ----------------------------------------------------------- line processing */

/* Pop and close frames the current line (at column `indent`) has dedented out
 * of. Returns 0, or -1 if a close emitted an error/cancel. */
static int reconcile(yatl_handle h, unsigned indent) {
    while (h->nframes > 0) {
        yatl_frame *f = &h->frames[h->nframes - 1];
        if (f->child_indent == U_UNSET) {
            if (indent <= f->header_indent) {
                if (close_frame(h, f) != 0) { h->nframes--; return -1; }
                h->nframes--;
                continue;
            }
            break;                               /* this line is the first child */
        }
        if (indent < f->child_indent) {
            if (close_frame(h, f) != 0) { h->nframes--; return -1; }
            h->nframes--;
            continue;
        }
        break;
    }
    return 0;
}

static void process_line(yatl_handle h) {
    const char *content = h->line.data ? (const char *)h->line.data : "";
    size_t clen = h->line.used;
    unsigned indent = 0;
    int is_item = 0;
    const char *val;
    size_t vlen;

    if (clen > 0 && content[clen - 1] == '\r')   /* strip CRLF's CR */
        clen--;

    while (indent < clen && content[indent] == ' ') indent++;
    if (indent) content += indent;
    clen -= indent;

    if (clen == 0)
        return;                                  /* blank line: ignore */

    if (content[0] == '-' && (clen == 1 || content[1] == ' ')) {
        is_item = 1;
        if (clen == 1) { val = content + 1; vlen = 0; }
        else { val = content + 2; vlen = clen - 2; }
    } else {
        val = content;
        vlen = clen;
    }

    if (h->state == ST_DOC_DONE) {
        if (!(h->flags & yatl_allow_trailing_garbage))
            set_err(h, "trailing content after document", h->line_off);
        return;
    }

    if (h->state == ST_START) {
        if (is_item) { set_err(h, "list item without an array header", h->line_off); return; }
        if (content[0] == '[') {                 /* root array */
            if (emit_array_header(h, content, content + clen, indent) != 0)
                return;
            h->state = (h->nframes == 0) ? ST_DOC_DONE : ST_BODY;
            return;
        }
        {                                        /* object vs root scalar */
            const char *ks = content, *ke = content;
            int is_obj_key = 0;
            if (content[0] == '"') {
                if (scan_token(content, content + clen, &ke) != 0) {
                    set_err(h, "unterminated quoted key", h->line_off); return;
                }
                is_obj_key = looks_like_key(ke, content + clen);
            } else if (is_key_start((unsigned char)content[0])) {
                (void)scan_token(content, content + clen, &ke);
                is_obj_key = (ke > ks && looks_like_key(ke, content + clen));
            }
            if (is_obj_key) {
                if (!open_frame(h, FR_OBJ, indent)) return;
                if (handle_member(h, h->nframes - 1, content, content + clen, indent) != 0) return;
                h->state = ST_BODY;
                return;
            }
            if (emit_scalar(h, content, clen, ',') != 0) return;   /* root scalar */
            h->state = ST_DOC_DONE;
            return;
        }
    }

    /* ST_BODY: reconcile against open containers, then attach. */
    if (reconcile(h, indent) != 0) return;
    if (h->nframes == 0) { set_err(h, "content after document root closed", h->line_off); return; }
    {
        size_t ti = h->nframes - 1;
        yatl_frame *f = &h->frames[ti];
        if (f->child_indent != U_UNSET && indent > f->child_indent) {
            set_err(h, "unexpected indentation", h->line_off);
            return;
        }
        switch (f->kind) {
            case FR_OBJ:
                if (is_item) { set_err(h, "list item in object", h->line_off); return; }
                handle_member(h, ti, content, content + clen, indent);
                break;
            case FR_ARR_LIST:
                if (!is_item) { set_err(h, "expected list item ('- ')", h->line_off); return; }
                handle_item(h, ti, val, val + vlen, indent);
                break;
            case FR_ARR_TAB:
                if (is_item) { set_err(h, "unexpected list item in tabular array", h->line_off); return; }
                handle_row(h, ti, content, content + clen, indent);
                break;
        }
    }
}

/* ------------------------------------------------------------------- drive */

yatl_status yatl_do_parse(yatl_handle h, const unsigned char *toonText,
                          size_t toonTextLen) {
    size_t base = h->stream_pos;
    size_t i;

    if (h->status != yatl_status_ok) {
        h->bytesConsumed = 0;
        return h->status;
    }

    for (i = 0; i < toonTextLen; i++) {
        char c = (char)toonText[i];
        if (c == '\n') {
            process_line(h);
            yatl_buf_clear(&h->line);
            h->stream_pos++;
            h->line_off = h->stream_pos;
            if (h->status != yatl_status_ok) {
                h->bytesConsumed = (h->err_off >= base) ? h->err_off - base : 0;
                return h->status;
            }
            continue;
        }
        if (yatl_buf_putc(&h->line, (unsigned char)c) != 0) {
            set_err(h, "out of memory", h->stream_pos);
            h->bytesConsumed = (h->err_off >= base) ? h->err_off - base : 0;
            return h->status;
        }
        h->stream_pos++;
    }
    h->bytesConsumed = toonTextLen;
    return yatl_status_ok;
}

yatl_status yatl_do_finish(yatl_handle h) {
    if (h->status != yatl_status_ok)
        return h->status;

    if (h->line.used > 0) {                       /* trailing unterminated line */
        process_line(h);
        yatl_buf_clear(&h->line);
        if (h->status != yatl_status_ok) { h->bytesConsumed = h->err_off; return h->status; }
    }

    if (h->state == ST_START) {                   /* empty document -> {} */
        if (emit_start_map(h) != 0 || emit_end_map(h) != 0) {
            h->bytesConsumed = h->err_off;
            return h->status;
        }
        h->state = ST_DOC_DONE;
    }

    while (h->nframes > 0) {                       /* close still-open containers */
        if (close_frame(h, &h->frames[h->nframes - 1]) != 0) { h->nframes--; h->bytesConsumed = h->err_off; return h->status; }
        h->nframes--;
    }
    return yatl_status_ok;
}

void yatl_free_frames(yatl_handle h) {
    size_t i;
    for (i = 0; i < h->nframes; i++)
        frame_free_cols(h, &h->frames[i]);
    if (h->frames)
        YA_FREE(&h->alloc, h->frames);
    h->frames = NULL;
    h->nframes = h->framecap = 0;
}

/* ------------------------------------------------------------- error string */

unsigned char *yatl_render_error_string(yatl_handle h,
                                        const unsigned char *toonText,
                                        size_t toonTextLen, int verbose) {
    size_t offset = h->bytesConsumed;
    const char *errorText = h->parseError ? h->parseError : "unknown error";
    unsigned char *str;
    char text[72];
    const char *arrow = "                     (right here) ------^\n";
    size_t memneeded = strlen("parse error: ") + strlen(errorText) + 2;

    str = (unsigned char *)YA_MALLOC(&h->alloc, memneeded);
    if (!str) return NULL;
    str[0] = 0;
    strcat((char *)str, "parse error: ");
    strcat((char *)str, errorText);
    strcat((char *)str, "\n");

    if (verbose && toonText) {
        size_t start, end, i;
        size_t spacesNeeded = (offset < 30 ? 40 - offset : 10);
        unsigned char *newStr;
        start = (offset >= 30 ? offset - 30 : 0);
        end = (offset + 30 > toonTextLen ? toonTextLen : offset + 30);
        for (i = 0; i < spacesNeeded; i++) text[i] = ' ';
        for (; start < end; start++, i++) {
            text[i] = (toonText[start] != '\n' && toonText[start] != '\r')
                          ? (char)toonText[start] : ' ';
        }
        text[i++] = '\n';
        text[i] = 0;
        newStr = (unsigned char *)YA_MALLOC(&h->alloc,
                     strlen((char *)str) + strlen(text) + strlen(arrow) + 1);
        if (newStr) {
            newStr[0] = 0;
            strcat((char *)newStr, (char *)str);
            strcat((char *)newStr, text);
            strcat((char *)newStr, arrow);
        }
        YA_FREE(&h->alloc, str);
        str = newStr;
    }
    return str;
}
