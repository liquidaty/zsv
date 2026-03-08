/* simdutf_wrapper.h - C-callable wrapper around simdutf */

#ifndef SIMDUTF_WRAPPER_H
#define SIMDUTF_WRAPPER_H

#include <stddef.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if valid UTF-8, 0 otherwise */
int simdutf_is_valid_utf8(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SIMDUTF_WRAPPER_H */
