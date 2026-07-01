/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * Public entry points. Structurally mirrors yajl.c: handle lifecycle, option
 * configuration, the push/finish parse calls, and error retrieval.
 */
#include <yatl/yatl_parse.h>
#include "yatl_parser.h"
#include "yatl_alloc.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

const char * yatl_status_to_string(yatl_status stat) {
    switch (stat) {
        case yatl_status_ok:              return "ok, no error";
        case yatl_status_client_canceled: return "client canceled parse";
        case yatl_status_error:           return "parse error";
    }
    return "unknown";
}

yatl_handle yatl_alloc(const yatl_callbacks *callbacks, yatl_alloc_funcs *afs,
                       void *ctx) {
    yatl_handle h;
    yatl_alloc_funcs afsBuffer;

    if (afs != NULL) {
        if (afs->malloc == NULL || afs->realloc == NULL || afs->free == NULL)
            return NULL;
    } else {
        yatl_set_default_alloc_funcs(&afsBuffer);
        afs = &afsBuffer;
    }

    h = (yatl_handle)YA_MALLOC(afs, sizeof(struct yatl_handle_t));
    if (!h)
        return NULL;
    memset(h, 0, sizeof *h);
    memcpy(&h->alloc, afs, sizeof(yatl_alloc_funcs));

    h->callbacks = callbacks;
    h->ctx = ctx;
    h->flags = 0;
    h->state = ST_START;
    h->status = yatl_status_ok;
    h->parseError = NULL;
    h->max_depth = YATL_MAX_DEPTH;
    yatl_buf_init(&h->line, &h->alloc);
    yatl_buf_init(&h->scratch, &h->alloc);
    return h;
}

const yatl_callbacks * yatl_swap_callbacks(yatl_handle h,
                                           const yatl_callbacks *callbacks,
                                           void *new_ctx) {
    const yatl_callbacks *old = h->callbacks;
    h->callbacks = callbacks;
    h->ctx = new_ctx;
    return old;
}

int yatl_config(yatl_handle h, yatl_option opt, ...) {
    int rv = 1;
    va_list ap;
    va_start(ap, opt);
    switch (opt) {
        case yatl_dont_validate_strings:
        case yatl_allow_trailing_garbage:
        case yatl_lenient_scalars:
        case yatl_dont_validate_length:
            if (va_arg(ap, int)) h->flags |= (unsigned)opt;
            else h->flags &= ~(unsigned)opt;
            break;
        case yatl_max_depth: {
            unsigned d = va_arg(ap, unsigned);
            if (d) h->max_depth = d;
            else rv = 0;                     /* zero depth is nonsensical */
            break;
        }
        default:
            rv = 0;
    }
    va_end(ap);
    return rv;
}

void yatl_free(yatl_handle h) {
    if (!h)
        return;
    yatl_free_frames(h);
    yatl_buf_free(&h->line);
    yatl_buf_free(&h->scratch);
    YA_FREE(&h->alloc, h);
}

yatl_status yatl_parse(yatl_handle hand, const unsigned char *toonText,
                       size_t toonTextLen) {
    return yatl_do_parse(hand, toonText, toonTextLen);
}

yatl_status yatl_complete_parse(yatl_handle hand) {
    return yatl_do_finish(hand);
}

unsigned char * yatl_get_error(yatl_handle hand, int verbose,
                               const unsigned char *toonText, size_t toonTextLen) {
    return yatl_render_error_string(hand, toonText, toonTextLen, verbose);
}

size_t yatl_get_bytes_consumed(yatl_handle hand) {
    return hand ? hand->bytesConsumed : 0;
}

void yatl_free_error(yatl_handle hand, unsigned char *str) {
    if (hand && str)
        YA_FREE(&hand->alloc, str);
}
