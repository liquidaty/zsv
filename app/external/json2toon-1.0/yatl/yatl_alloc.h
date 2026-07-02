/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * Default memory allocation routines (malloc/realloc/free) and the YA_*
 * convenience macros used throughout the library.
 */
#ifndef __YATL_ALLOC_H__
#define __YATL_ALLOC_H__

#include <yatl/yatl_common.h>

#define YA_MALLOC(afs, sz) (afs)->malloc((afs)->ctx, (sz))
#define YA_FREE(afs, ptr) (afs)->free((afs)->ctx, (ptr))
#define YA_REALLOC(afs, ptr, sz) (afs)->realloc((afs)->ctx, (ptr), (sz))

void yatl_set_default_alloc_funcs(yatl_alloc_funcs *yaf);

#endif
