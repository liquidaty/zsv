/*
 * SIMD abstraction for the fast CSV scanner — x86_64 AVX2 (256-bit).
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t       — broadcast vector type
 *   fast_vec_set1()  — broadcast a byte to all lanes
 *   fast_cmpeq_64()  — compare 64 bytes against a broadcast value,
 *                       return 64-bit bitmask (bit N set ↔ byte N matched)
 *
 * AVX2 processes 64 bytes as 2 × 32-byte loads (half the loads of SSE2).
 */

#ifndef ZSV_SCAN_SIMD_AVX2_H
#define ZSV_SCAN_SIMD_AVX2_H

#include <immintrin.h>

typedef __m256i fast_vec_t;

static inline fast_vec_t fast_vec_set1(unsigned char c) {
  return _mm256_set1_epi8((char)c);
}

/* Compare 64 bytes against broadcast vector, return 64-bit bitmask. */
__attribute__((always_inline)) static inline uint64_t fast_cmpeq_64(const unsigned char *p, fast_vec_t v) {
  __m256i b0 = _mm256_loadu_si256((const __m256i *)p);
  __m256i b1 = _mm256_loadu_si256((const __m256i *)(p + 32));
  uint64_t m0 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b0, v));
  uint64_t m1 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b1, v));
  return m0 | (m1 << 32);
}

#endif /* ZSV_SCAN_SIMD_AVX2_H */
