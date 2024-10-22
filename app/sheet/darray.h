/* Simple dynamic array
 */
#ifndef ZSVSHEET_DARRAY_H
#define ZSVSHEET_DARRAY_H
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

struct darray {
  size_t cap;
  size_t cnt;
  size_t esz;
  void * mem;
};

int darray_init(struct darray *arr, size_t esz);

bool darray_alive(const struct darray *arr);

int darray_realloc(struct darray * arr, size_t ncap);

void *darray_insert(struct darray *arr, const void *e, size_t idx);

void *darray_push(struct darray * arr, const void * e);

void *darray_at(struct darray *arr, size_t idx);

size_t darray_idx(const struct darray *arr, const void *e);
#define darray_at_t(TYPE, ARR, IDX) ((TYPE*)darray_at(ARR, IDX))

void * darray_fst(struct darray * arr);

void * darray_next(struct darray * arr, const void *e);

#define darray_for(TYPE, VAR, ARR) \
  for(TYPE * VAR = darray_alive(ARR) ? darray_fst(ARR) : NULL; (VAR); VAR = darray_next(ARR, VAR))

#endif /* ZSVSHEET_DARRAY_H */
