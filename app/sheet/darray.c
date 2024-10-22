#include "darray.h"
#include <assert.h>
#include <string.h>

static inline void * _darray_xalloc(void *ptr, size_t sz) 
{
  return (!sz ? (free(ptr), NULL) : (!ptr ? malloc(sz) : realloc(ptr, sz)));
}

int darray_init(struct darray * arr, size_t esz)
{
  assert(arr && esz);
  memset(arr, 0, sizeof(*arr));
  arr->esz = esz;
  return 0;
}

bool darray_alive(const struct darray *arr)
{ return arr && arr->esz != 0; }


#define _darray_ptrdiff(A,B) (((uint8_t*)A) - ((uint8_t*)B))

#define _darray_idx(ARR, E)\
  (size_t)( _darray_ptrdiff(E, arr->mem) / arr->esz)

size_t darray_idx(const struct darray *arr, const void *e) 
{
  assert(darray_alive(arr));
  return _darray_idx(arr, e);
}

void * darray_fst(struct darray * arr) 
{ return darray_at(arr, 0); }

void * darray_next(struct darray * arr, const void *e)
{ return darray_at(arr, _darray_idx(arr, e) + 1); }


/* Use only if you know that IDX < (ARR)->cap */
#define _darray_at(ARR, IDX)\
  ((void*)((uint8_t*)(ARR)->mem + (ARR)->esz * (IDX)))

void *darray_at(struct darray *arr, size_t idx)
{
  assert(darray_alive(arr));
  return (idx < arr->cnt) ? _darray_at(arr, idx) : NULL;
}

int darray_realloc(struct darray * arr, size_t ncap)
{
  assert(darray_alive(arr));
  assert(arr->cnt <= ncap);

  void *nmem = _darray_xalloc(arr->mem, ncap * arr->esz);
  if(ncap)
    assert(nmem);
  else
    nmem = NULL;

  arr->cap = ncap;
  arr->mem = nmem;
  return 0;
}


static void _darray_zero_range(struct darray *arr, size_t b, size_t e)
{
  assert( b <= e );
  memset((uint8_t*)arr->mem + b * arr->esz, '\0', e - b);
}

int darray_resize(struct darray *arr, size_t new_cnt)
{
  int ret = -1;
  if(arr->cnt < new_cnt)  {
    if(darray_realloc(arr, new_cnt))
      goto exit;

    _darray_zero_range(arr, arr->cnt, new_cnt);
  }
  arr->cnt = new_cnt;

  ret = 0;
 exit:
  return ret;
}

static inline
void _darray_expand(struct darray * arr, size_t idx, size_t n) 
{
  if(n == 0) return;

  uint8_t * from = _darray_at(arr, idx);
  uint8_t * to = from + arr->esz * n;

  assert(arr->cnt >= idx);
  size_t size = arr->esz * (arr->cnt - idx);

  memmove(to, from, size);
  memset(from, 0, (to - from));
}

int darray_grow(struct darray * arr, size_t nmore) 
{
  size_t ncap = arr->cap ? arr->cap * 2 : 1; 
  if(ncap < nmore) ncap = nmore;
  return darray_realloc(arr, ncap);
}

void *darray_insert(struct darray *arr, const void *e, size_t idx) 
{
  int rc;
  assert(idx <= arr->cnt);

  if(arr->cnt == arr->cap) {
    rc = darray_grow(arr, 1);
    assert(rc == 0);
  }

  _darray_expand(arr, idx, 1);
  arr->cnt++;

  if(e) return memcpy(_darray_at(arr, idx), e, arr->esz);
  else  return memset(_darray_at(arr, idx), 0, arr->esz);
}

void * darray_push(struct darray * arr, const void * e) 
{
  return darray_insert(arr, e, arr->cnt);
}
