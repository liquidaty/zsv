/*
 * SIMD abstraction for the fast CSV scanner — WebAssembly SIMD128.
 *
 * Provides the platform-specific primitives used by zsv_scan_delim_fast.c:
 *   fast_vec_t            — broadcast vector type
 *   fast_vec_set1()       — broadcast a byte to all lanes
 *   fast_cmpeq_64()       — compare 64 bytes against one value → 64-bit mask
 *   fast_scan_block()     — load 64 bytes once, compare against 4 values
 *   fast_prefix_xor()     — cumulative XOR (quote state propagation)
 *   fast_clear_lowest()   — clear lowest set bit
 *
 * This implementation uses WebAssembly SIMD (128-bit) via <wasm_simd128.h>.
 * WebAssembly SIMD currently supports 128-bit vectors maximum.
 * Requires the -msimd128 compiler flag.
 */

#ifndef ZSV_SCAN_SIMD_WASM_H
#define ZSV_SCAN_SIMD_WASM_H

#include <wasm_simd128.h>
#include <stdint.h>

typedef v128_t fast_vec_t;

static inline fast_vec_t fast_vec_set1(unsigned char c) {
  return wasm_i8x16_splat((char)c);
}

/* Compare 64 bytes against broadcast vector, return 64-bit bitmask. */
__attribute__((always_inline)) static inline uint64_t fast_cmpeq_64(const unsigned char *p, fast_vec_t v) {
  uint64_t mask = 0;
  for (int i = 0; i < 4; i++) {
    v128_t b = wasm_v128_load((const v128_t *)(p + i * 16));
    mask |= ((uint64_t)(uint16_t)wasm_i8x16_bitmask(wasm_i8x16_eq(b, v))) << (i * 16);
  }
  return mask;
}

/*
 * Load 64 bytes once, compare against 4 broadcast vectors, produce 4 masks.
 * Minimizes loads by reusing the 128-bit registers for multiple comparisons.
 */
__attribute__((always_inline)) static inline void fast_scan_block(const unsigned char *p, fast_vec_t v0, fast_vec_t v1,
                                                                  fast_vec_t v2, fast_vec_t v3, uint64_t *m0,
                                                                  uint64_t *m1, uint64_t *m2, uint64_t *m3) {
  *m0 = 0;
  *m1 = 0;
  *m2 = 0;
  *m3 = 0;
  for (int i = 0; i < 4; i++) {
    v128_t b = wasm_v128_load((const v128_t *)(p + i * 16));
    *m0 |= ((uint64_t)(uint16_t)wasm_i8x16_bitmask(wasm_i8x16_eq(b, v0))) << (i * 16);
    *m1 |= ((uint64_t)(uint16_t)wasm_i8x16_bitmask(wasm_i8x16_eq(b, v1))) << (i * 16);
    *m2 |= ((uint64_t)(uint16_t)wasm_i8x16_bitmask(wasm_i8x16_eq(b, v2))) << (i * 16);
    *m3 |= ((uint64_t)(uint16_t)wasm_i8x16_bitmask(wasm_i8x16_eq(b, v3))) << (i * 16);
  }
}

/*
 * Cumulative XOR (prefix XOR) — propagates quote toggle state across 64 bits.
 * WebAssembly lacks a native carry-less multiply (PCLMULQDQ);
 * using the 6-round shift-XOR cascade.
 */
__attribute__((always_inline)) static inline uint64_t fast_prefix_xor(uint64_t x) {
  x ^= x << 1;
  x ^= x << 2;
  x ^= x << 4;
  x ^= x << 8;
  x ^= x << 16;
  x ^= x << 32;
  return x;
}

/* Clear lowest set bit. */
__attribute__((always_inline)) static inline uint64_t fast_clear_lowest(uint64_t x) {
  return x & (x - 1);
}

#endif /* ZSV_SCAN_SIMD_WASM_H */
