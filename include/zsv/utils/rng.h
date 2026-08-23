/*
 * Copyright (C) 2025 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_RNG_H
#define ZSV_RNG_H

#include <stdint.h>

/**
 * Small, self-contained, platform-independent PRNG (xoshiro256**, seeded via
 * splitmix64). Not cryptographic. A given seed yields the same sequence on
 * every compiler and OS, so seeded output is reproducible across platforms.
 */
struct zsv_rng {
  uint64_t s[4];
};

/** Initialize from a 64-bit seed; any seed (including 0) is valid */
void zsv_rng_seed(struct zsv_rng *r, uint64_t seed);

/** 64 bits from the best OS entropy source available; never fails */
uint64_t zsv_rng_entropy(void);

/** Next 64-bit value */
uint64_t zsv_rng_next(struct zsv_rng *r);

/** Unbiased value in [0, n). n == 0 returns 0 */
uint64_t zsv_rng_below(struct zsv_rng *r, uint64_t n);

#endif
