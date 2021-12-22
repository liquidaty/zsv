/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood (self), Matt Wong (Guarnerix Inc dba Liquidaty)
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define clear_lowest_bit(n) (n & (n - 1)) // _blsr_u64(n) // seems to be same speed as x = x & (x - 1)

// vec_delims: return bitfield of next 32 bytes that contain at least 1 token
static inline int vec_delims(const unsigned char *s, size_t n,
                             zsv_uc_vector *char_match1,
                             zsv_uc_vector *char_match2,
                             zsv_uc_vector *char_match3,
                             zsv_uc_vector *char_match4,
                             unsigned int *maskp
                             ) {
  zsv_uc_vector* pSrc1 = (zsv_uc_vector *)s;
  zsv_uc_vector str_simd;

  unsigned j = n / VECTOR_BYTES;
  unsigned int mask = 0;
  unsigned total_bytes = 0;

  for(unsigned i = 0; i < j; i++) {
    memcpy(&str_simd, pSrc1 + i, VECTOR_BYTES);
    zsv_uc_vector vtmp = str_simd == *char_match1;
    vtmp += (str_simd == *char_match2);
    vtmp += (str_simd == *char_match3);
    vtmp += (str_simd == *char_match4);
    mask = movemask_pseudo(vtmp);
    if(LIKELY(mask != 0)) { // check if we found one of the 4 chars
      *maskp = mask;
      return total_bytes;
    } else {
      // not found, moving to next chunk. first, check if this chunk has multichar bytes
      total_bytes += sizeof(*pSrc1);
    }
  }
  return total_bytes; // nothing found in entire buffer
}
