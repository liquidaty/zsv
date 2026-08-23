/*
 * Copyright (C) 2025 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

// must precede every include: glibc and wasi-libc hide arc4random()/getentropy()
// behind _GNU_SOURCE (see os.c)
#define _GNU_SOURCE 1
#ifdef _WIN32
#define _CRT_RAND_S // rand_s(); must precede <stdlib.h>
#endif
#include <stdlib.h>
#include <time.h>
#if defined(HAVE_GETENTROPY)
#include <unistd.h>
#endif
#include <zsv/utils/rng.h>

static inline uint64_t zsv_rng_rotl(uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

static uint64_t zsv_rng_splitmix64(uint64_t *x) {
  uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void zsv_rng_seed(struct zsv_rng *r, uint64_t seed) {
  for (int i = 0; i < 4; i++)
    r->s[i] = zsv_rng_splitmix64(&seed);
  // the all-zero state is a fixed point; splitmix64 cannot produce four zeros
  // from one stream, so no further check is needed
}

uint64_t zsv_rng_entropy(void) {
#if defined(HAVE_ARC4RANDOM_UNIFORM) // arc4random() is available wherever arc4random_uniform() is
  return ((uint64_t)arc4random() << 32) | arc4random();
#else
  uint64_t v = 0;
#if defined(HAVE_GETENTROPY)
  if (getentropy(&v, sizeof(v)) == 0)
    return v;
#endif
  // weak fallback: wall clock, stack address and a call counter (so two calls within one
  // second still differ), whitened through splitmix64
  static uint64_t calls;
  uint64_t t = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&t ^ (++calls << 48);
  v = zsv_rng_splitmix64(&t);
#if defined(HAVE_RAND_S)
  unsigned int hi = 0, lo = 0;
  // rand_s() fails only on a broken CRT; the values then stay 0 and the fallback mix still applies
  (void)rand_s(&hi);
  (void)rand_s(&lo);
  v ^= ((uint64_t)hi << 32) | lo;
#endif
  return v;
#endif
}

uint64_t zsv_rng_next(struct zsv_rng *r) {
  uint64_t *s = r->s;
  const uint64_t result = zsv_rng_rotl(s[1] * 5, 7) * 9;
  const uint64_t t = s[1] << 17;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = zsv_rng_rotl(s[3], 45);
  return result;
}

uint64_t zsv_rng_below(struct zsv_rng *r, uint64_t n) {
  if (n < 2)
    return 0;
  // mask to the smallest 2^k-1 >= n-1, then reject draws >= n: unbiased, < 2 draws expected
  uint64_t mask = n - 1;
  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  mask |= mask >> 16;
  mask |= mask >> 32;
  uint64_t v;
  do
    v = zsv_rng_next(r) & mask;
  while (v >= n);
  return v;
}
