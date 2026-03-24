/*
 * SIMD abstraction for the fast CSV scanner — x86_64 SSE2 (128-bit).
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t       — broadcast vector type
 *   fast_vec_set1()  — broadcast a byte to all lanes
 *   fast_cmpeq_64()  — compare 64 bytes against a broadcast value,
 *                       return 64-bit bitmask (bit N set ↔ byte N matched)
 *
 * SSE2 is baseline for all x86_64. Uses 4 × 16-byte loads per 64 bytes.
 * The native _mm_movemask_epi8 compiles to a single instruction.
 */

#ifndef ZSV_SCAN_SIMD_SSE2_H
#define ZSV_SCAN_SIMD_SSE2_H

#include <emmintrin.h>

typedef __m128i fast_vec_t;

static inline fast_vec_t fast_vec_set1(unsigned char c) {
  return _mm_set1_epi8((char)c);
}

/* Compare 64 bytes against broadcast vector, return 64-bit bitmask. */
__attribute__((always_inline)) static inline uint64_t fast_cmpeq_64(const unsigned char *p, fast_vec_t v) {
  uint64_t mask = 0;
  for (int i = 0; i < 4; i++) {
    __m128i b = _mm_loadu_si128((const __m128i *)(p + i * 16));
    mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(b, v)) << (i * 16);
  }
  return mask;
}

#endif /* ZSV_SCAN_SIMD_SSE2_H */
