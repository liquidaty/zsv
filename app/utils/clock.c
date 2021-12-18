/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/clock.h>

clock_t zsv_clock_begin;
clock_t zsv_clock_in;
clock_t zsv_clock_out;
int i_tmp;

size_t
zsv_fread_clock(void *restrict ptr, size_t size, size_t nitems,
                FILE *restrict stream) {
  clock_t clock_tmp = clock();
  size_t sz = fread(ptr, size, nitems, stream);
  zsv_clock_in += clock() - clock_tmp;
  return sz;
}

size_t
zsv_fwrite_clock(const void *restrict ptr, size_t size, size_t nitems,
                 FILE *restrict stream) {
  clock_t clock_tmp = clock();
  size_t sz = fwrite(ptr, size, nitems, stream);
  zsv_clock_out += clock() - clock_tmp;
  return sz;
}

int
zsv_fflush_clock(FILE *stream) {
  clock_t clock_tmp = clock();
  int i = fflush(stream);
  zsv_clock_out += clock() - clock_tmp;
  return i;
}

void
zsv_clocks_begin() {
  zsv_clock_in = zsv_clock_out = 0;
  zsv_clock_begin = clock();
}

void
zsv_clocks_end() {
  clock_t clock_end = clock();
  clock_t clock_total = clock_end - zsv_clock_begin;
  clock_t clock_other = clock_total - zsv_clock_in - zsv_clock_out;
  fprintf(stderr, "elapsed time:\n"
          "  total %zu, %Lf\n"
          "  in %zu, %Lf\n"
          "  out %zu, %Lf\n"
          "  other %zu, %Lf\n"
          "\n",
          (size_t)(clock_total), (long double)(clock_total) / CLOCKS_PER_SEC,
          (size_t)zsv_clock_in, (long double)(zsv_clock_in) / CLOCKS_PER_SEC,
          (size_t)zsv_clock_out, (long double)(zsv_clock_out) / CLOCKS_PER_SEC,
          (size_t)clock_other, (long double)(clock_other) / CLOCKS_PER_SEC
          );
}
