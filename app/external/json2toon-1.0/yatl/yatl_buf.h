/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * A growable byte buffer with geometric growth. Embedded by value; backed by
 * the handle's allocator. Unlike yajl's buffer, every append reports OOM
 * (returns -1) rather than risking a NULL dereference, so the parser can fail
 * cleanly with yatl out-of-memory handling.
 */
#ifndef __YATL_BUF_H__
#define __YATL_BUF_H__

#include "yatl_alloc.h"

typedef struct {
    unsigned char *data;
    size_t used;
    size_t len;            /* allocated capacity */
    yatl_alloc_funcs *alloc;
} yatl_buf;

/* Bind a (zeroed) buffer to an allocator. Allocates nothing yet. */
void yatl_buf_init(yatl_buf *buf, yatl_alloc_funcs *alloc);

/* Append len bytes. Returns 0 on success, -1 on OOM. */
int yatl_buf_append(yatl_buf *buf, const void *data, size_t len);

/* Append one byte. Returns 0 on success, -1 on OOM. */
int yatl_buf_putc(yatl_buf *buf, unsigned char c);

/* Reset length to zero (keeps the allocation). */
void yatl_buf_clear(yatl_buf *buf);

/* Release storage and reset to the post-init state. */
void yatl_buf_free(yatl_buf *buf);

#endif
