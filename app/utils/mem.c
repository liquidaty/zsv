/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h> // calloc
#include <string.h> // memcpy

/* zsv_memdup(): return copy with double-NULL terminator. caller must free() */
void *zsv_memdup(const void *src, size_t n) {
  void *m = calloc(1, n + 2);
  if(n)
    memcpy(m, src, n);
  return m;
}
