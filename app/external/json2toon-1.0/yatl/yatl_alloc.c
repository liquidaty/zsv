/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 */
#include "yatl_alloc.h"
#include <stdlib.h>

static void * yatl_internal_malloc(void *ctx, size_t sz) {
    (void)ctx;
    return malloc(sz);
}

static void * yatl_internal_realloc(void *ctx, void *previous, size_t sz) {
    (void)ctx;
    return realloc(previous, sz);
}

static void yatl_internal_free(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

void yatl_set_default_alloc_funcs(yatl_alloc_funcs *yaf) {
    yaf->malloc = yatl_internal_malloc;
    yaf->realloc = yatl_internal_realloc;
    yaf->free = yatl_internal_free;
    yaf->ctx = NULL;
}
