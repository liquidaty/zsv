/*
 * SIMD abstraction for the fast CSV scanner — x86_64 AVX2 (256-bit).
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t            — broadcast vector type
 *   fast_vec_set1()       — broadcast a byte to all lanes
 *   fast_cmpeq_64()       — compare 64 bytes against one value → 64-bit mask
 *   fast_scan_block()     — load 64 bytes once, compare against 4 values
 *   fast_prefix_xor()     — cumulative XOR (quote state propagation)
 *   fast_clear_lowest()   — clear lowest set bit
 *
 * AVX2 processes 64 bytes as 2 × 32-byte loads (half the loads of SSE2).
 */

#ifndef ZSV_SCAN_SIMD_AVX2_H
#define ZSV_SCAN_SIMD_AVX2_H

#include <immintrin.h>
#include <stdint.h>

#ifdef __PCLMUL__
#include <wmmintrin.h> /* PCLMULQDQ */
#endif

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

/*
 * Load 64 bytes once, compare against 4 broadcast vectors, produce 4 masks.
 * Halves the number of loads vs calling fast_cmpeq_64 four times.
 */
__attribute__((always_inline)) static inline void fast_scan_block(const unsigned char *p, fast_vec_t v0, fast_vec_t v1,
                                                                  fast_vec_t v2, fast_vec_t v3, uint64_t *m0,
                                                                  uint64_t *m1, uint64_t *m2, uint64_t *m3) {
  __m256i lo = _mm256_loadu_si256((const __m256i *)p);
  __m256i hi = _mm256_loadu_si256((const __m256i *)(p + 32));
  *m0 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lo, v0)) |
        ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hi, v0)) << 32);
  *m1 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lo, v1)) |
        ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hi, v1)) << 32);
  *m2 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lo, v2)) |
        ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hi, v2)) << 32);
  *m3 = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(lo, v3)) |
        ((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(hi, v3)) << 32);
}

/*
 * Cumulative XOR (prefix XOR) — propagates quote toggle state across 64 bits.
 * With PCLMULQDQ this is a single carry-less multiply instruction;
 * without it, falls back to the 6-round shift-XOR cascade.
 */
__attribute__((always_inline)) static inline uint64_t fast_prefix_xor(uint64_t x) {
#ifdef __PCLMUL__
  __m128i v = _mm_set_epi64x(0, (long long)x);
  __m128i ones = _mm_set_epi64x(0, (long long)~0ULL);
  return (uint64_t)_mm_cvtsi128_si64(_mm_clmulepi64_si128(v, ones, 0));
#else
  x ^= x << 1;
  x ^= x << 2;
  x ^= x << 4;
  x ^= x << 8;
  x ^= x << 16;
  x ^= x << 32;
  return x;
#endif
}

/* Clear lowest set bit — BMI1 blsr or portable fallback. */
__attribute__((always_inline)) static inline uint64_t fast_clear_lowest(uint64_t x) {
#ifdef __BMI__
  return _blsr_u64(x);
#else
  return x & (x - 1);
#endif
}

#endif /* ZSV_SCAN_SIMD_AVX2_H */
