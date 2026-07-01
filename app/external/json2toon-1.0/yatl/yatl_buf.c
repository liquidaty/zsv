/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 */
#include "yatl_buf.h"
#include <string.h>

#define YATL_BUF_INIT_SIZE 256

void yatl_buf_init(yatl_buf *buf, yatl_alloc_funcs *alloc) {
    buf->data = NULL;
    buf->used = 0;
    buf->len = 0;
    buf->alloc = alloc;
}

/* Ensure room for `want` more bytes. 0 on success, -1 on OOM. */
static int yatl_buf_ensure(yatl_buf *buf, size_t want) {
    size_t need = buf->len ? buf->len : YATL_BUF_INIT_SIZE;
    unsigned char *nd;
    if (want <= buf->len - buf->used)
        return 0;
    while (want > need - buf->used) {
        if (need > (size_t)-1 / 2)        /* would overflow on doubling */
            return -1;
        need <<= 1;
    }
    nd = (unsigned char *)YA_REALLOC(buf->alloc, buf->data, need);
    if (!nd)
        return -1;
    buf->data = nd;
    buf->len = need;
    return 0;
}

int yatl_buf_append(yatl_buf *buf, const void *data, size_t len) {
    if (len == 0)
        return 0;
    if (yatl_buf_ensure(buf, len) != 0)
        return -1;
    memcpy(buf->data + buf->used, data, len);
    buf->used += len;
    return 0;
}

int yatl_buf_putc(yatl_buf *buf, unsigned char c) {
    if (yatl_buf_ensure(buf, 1) != 0)
        return -1;
    buf->data[buf->used++] = c;
    return 0;
}

void yatl_buf_clear(yatl_buf *buf) {
    buf->used = 0;
}

void yatl_buf_free(yatl_buf *buf) {
    if (buf->data)
        YA_FREE(buf->alloc, buf->data);
    buf->data = NULL;
    buf->used = 0;
    buf->len = 0;
}
