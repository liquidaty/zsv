/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_SELECT_SAMPLE_H
#define ZSV_SELECT_SAMPLE_H

#include <stdint.h>
#include <zsv/utils/rng.h>

// one retained row of a reservoir sample: the projected output cells, serialized as
// [size_t len][char quoted][len bytes] per output column
struct zsv_select_sample_slot {
  size_t row_ix; // data row number at capture; sort key and -N value
  unsigned char *buf;
  size_t len, cap;
};

// --sample-size state. Rewindable input: count pass + selection sampling (Vitter's
// Algorithm S), O(1) memory. Otherwise: reservoir sampling (Algorithm R) of the
// projected rows, emitted in input order at end of input
struct zsv_select_sample {
  size_t size;   // --sample-size; 0 = off
  uint64_t seed; // --seed
  struct zsv_rng rng;
  uint64_t pct_threshold; // --sample-pct as a count out of 1,000,000 draws

  size_t pop_remaining, want_remaining; // Algorithm S

  struct zsv_select_sample_slot *slots; // Algorithm R: grows on demand up to size entries; NULL on the 2-pass path
  size_t slots_cap, filled, seen;

  unsigned char have_seed : 1;
  unsigned char reservoir : 1; // input is not rewindable (or overwrites are active)
  unsigned char failed : 1;    // reservoir allocation failed mid-parse
};

#endif
