/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * Internal parser state. TOON is indentation-structured, so the parser is a
 * line-oriented pushdown machine: input is buffered only up to the current
 * newline, each completed line is reconciled against a stack of open containers
 * (opened/closed by indentation changes), and callback events are emitted as
 * soon as each line is understood. Peak memory is a function of nesting depth
 * and the widest single line (e.g. one tabular row), never of total input.
 */
#ifndef __YATL_PARSER_H__
#define __YATL_PARSER_H__

#include <yatl/yatl_parse.h>
#include "yatl_buf.h"

/* Container kinds on the parse stack. */
enum { FR_OBJ, FR_ARR_LIST, FR_ARR_TAB };

/* Document-level state. */
enum { ST_START, ST_BODY, ST_DOC_DONE, ST_FAIL, ST_CANCELED };

typedef struct {
    int kind;
    unsigned header_indent;     /* indent column of the line that opened this frame */
    unsigned child_indent;      /* indent column of children; U_UNSET until seen */
    size_t emitted;             /* children emitted so far */
    size_t expected;            /* declared element count (arrays) */
    int has_expected;           /* whether `expected` was given */
    unsigned char delim;        /* active delimiter (',' '\t' '|') for this scope */
    /* tabular only: decoded column names */
    unsigned char **cols;
    size_t *collen;
    size_t ncols;
} yatl_frame;

struct yatl_handle_t {
    const yatl_callbacks *callbacks;
    void *ctx;
    yatl_alloc_funcs alloc;
    unsigned int flags;         /* yatl_option bitfield */

    int state;                  /* ST_* */
    yatl_status status;         /* sticky non-ok status once set */
    const char *parseError;     /* human-readable message (static string) */
    size_t err_off;             /* stream offset where the error was detected */
    size_t bytesConsumed;       /* yajl-style: per-chunk consumed / error offset */

    size_t stream_pos;          /* total input bytes consumed */
    size_t line_off;            /* stream offset of the first byte of `line` */

    yatl_buf line;              /* current line accumulator (no newline) */
    yatl_buf scratch;           /* decode scratch for strings / keys / numbers */

    yatl_frame *frames;
    size_t nframes, framecap;
    unsigned max_depth;
};

yatl_status yatl_do_parse(yatl_handle h, const unsigned char *toonText,
                          size_t toonTextLen);

yatl_status yatl_do_finish(yatl_handle h);

unsigned char *yatl_render_error_string(yatl_handle h,
                                        const unsigned char *toonText,
                                        size_t toonTextLen, int verbose);

/* Release the frame stack and any column storage it owns. */
void yatl_free_frames(yatl_handle h);

#endif
