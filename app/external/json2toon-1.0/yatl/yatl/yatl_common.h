/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * yatl - a streaming, event-driven (SAX) parser for TOON (Token-Oriented
 * Object Notation). The public API deliberately mirrors yajl's so that code
 * written against yajl ports to TOON with only a prefix change.
 */
#ifndef __YATL_COMMON_H__
#define __YATL_COMMON_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum container nesting depth accepted by the parser. */
#define YATL_MAX_DEPTH 128

/* DLL export decoration. To build a Windows DLL define YATL_SHARED and
 * YATL_BUILD; to consume one define YATL_SHARED. */
#if (defined(_WIN32) || defined(WIN32)) && defined(YATL_SHARED)
#  ifdef YATL_BUILD
#    define YATL_API __declspec(dllexport)
#  else
#    define YATL_API __declspec(dllimport)
#  endif
#else
#  if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 303
#    define YATL_API __attribute__ ((visibility("default")))
#  else
#    define YATL_API
#  endif
#endif

/** pointer to a malloc function, supporting client overriding memory
 *  allocation routines */
typedef void * (*yatl_malloc_func)(void *ctx, size_t sz);

/** pointer to a free function, supporting client overriding memory
 *  allocation routines */
typedef void (*yatl_free_func)(void *ctx, void *ptr);

/** pointer to a realloc function which can resize an allocation. */
typedef void * (*yatl_realloc_func)(void *ctx, void *ptr, size_t sz);

/** A structure which can be passed to yatl_alloc to allow the client to
 *  specify memory allocation functions to be used. */
typedef struct {
    /** pointer to a function that can allocate uninitialized memory */
    yatl_malloc_func malloc;
    /** pointer to a function that can resize memory allocations */
    yatl_realloc_func realloc;
    /** pointer to a function that can free memory */
    yatl_free_func free;
    /** a context pointer passed to the above allocation routines */
    void *ctx;
} yatl_alloc_funcs;

#ifdef __cplusplus
}
#endif

#endif
