/*
 * SIMD abstraction for the fast CSV scanner — aarch64 NEON.
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t       — broadcast vector type
 *   fast_vec_set1()  — broadcast a byte to all lanes
 *   fast_cmpeq_64()  — compare 64 bytes against a broadcast value,
 *                       return 64-bit bitmask (bit N set ↔ byte N matched)
 */

#ifndef ZSV_SCAN_SIMD_NEON_H
#define ZSV_SCAN_SIMD_NEON_H

#include <arm_neon.h>

typedef uint8x16_t fast_vec_t;

static inline fast_vec_t fast_vec_set1(unsigned char c) {
  return vdupq_n_u8(c);
}

/* NEON has no native movemask — emulate with weighted horizontal add. */
__attribute__((always_inline)) static inline uint16_t fast_neon_movemask(uint8x16_t input) {
  static const uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
  const uint8x16_t bit_weights = vld1q_u8(weights);
  uint8x16_t masked = vandq_u8(input, bit_weights);
  return (uint16_t)vaddv_u8(vget_low_u8(masked)) | ((uint16_t)vaddv_u8(vget_high_u8(masked)) << 8);
}

/* Compare 64 bytes against broadcast vector, return 64-bit bitmask. */
__attribute__((always_inline)) static inline uint64_t fast_cmpeq_64(const unsigned char *p, fast_vec_t v) {
  uint64_t mask = 0;
  for (int i = 0; i < 4; i++) {
    uint8x16_t b = vld1q_u8(p + i * 16);
    mask |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v)) << (i * 16);
  }
  return mask;
}

#endif /* ZSV_SCAN_SIMD_NEON_H */
