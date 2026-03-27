/*
 * SIMD abstraction for the fast CSV scanner — x86_64 SSE2 (128-bit).
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t            — broadcast vector type
 *   fast_vec_set1()       — broadcast a byte to all lanes
 *   fast_cmpeq_64()       — compare 64 bytes against one value → 64-bit mask
 *   fast_scan_block()     — load 64 bytes once, compare against 4 values
 *   fast_prefix_xor()     — cumulative XOR (quote state propagation)
 *   fast_clear_lowest()   — clear lowest set bit
 *
 * SSE2 is baseline for all x86_64. Uses 4 × 16-byte loads per 64 bytes.
 * The native _mm_movemask_epi8 compiles to a single instruction.
 */

#ifndef ZSV_SCAN_SIMD_SSE2_H
#define ZSV_SCAN_SIMD_SSE2_H

#include <emmintrin.h>

#ifdef __PCLMUL__
#include <wmmintrin.h> /* PCLMULQDQ */
#endif

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

/*
 * Load 64 bytes once, compare against 4 broadcast vectors, produce 4 masks.
 * Avoids redundant loads vs calling fast_cmpeq_64 four times.
 */
__attribute__((always_inline)) static inline void fast_scan_block(const unsigned char *p, fast_vec_t v0, fast_vec_t v1,
                                                                  fast_vec_t v2, fast_vec_t v3, uint64_t *m0,
                                                                  uint64_t *m1, uint64_t *m2, uint64_t *m3) {
  *m0 = 0;
  *m1 = 0;
  *m2 = 0;
  *m3 = 0;
  for (int i = 0; i < 4; i++) {
    __m128i b = _mm_loadu_si128((const __m128i *)(p + i * 16));
    unsigned shift = i * 16;
    *m0 |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(b, v0)) << shift;
    *m1 |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(b, v1)) << shift;
    *m2 |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(b, v2)) << shift;
    *m3 |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(b, v3)) << shift;
  }
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

#endif /* ZSV_SCAN_SIMD_SSE2_H */
