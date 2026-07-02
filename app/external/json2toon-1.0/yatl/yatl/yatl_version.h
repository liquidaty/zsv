/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 */
#ifndef __YATL_VERSION_H__
#define __YATL_VERSION_H__

#include <yatl/yatl_common.h>

/* Single source of truth for the library version. ./configure and the Makefile
 * both derive the package version from YATL_VERSION below. */
#define YATL_MAJOR 1
#define YATL_MINOR 0
#define YATL_MICRO 0
#define YATL_VERSION "1.0.0"
#define YATL_VERSION_NUMBER (YATL_MAJOR * 10000 + YATL_MINOR * 100 + YATL_MICRO)

#ifdef __cplusplus
extern "C" {
#endif

/** Return the compiled-in library version as a single integer
 *  (major*10000 + minor*100 + micro). */
YATL_API int yatl_version(void);

#ifdef __cplusplus
}
#endif

#endif
