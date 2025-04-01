#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "../include/memfile.h"

struct memfile {
  void *mem;
  size_t allocated, used;
};

off_t memfile_tell(memfile_t mf) {
  return mf->used;
}

void *memfile_data(memfile_t mf) {
  return mf->mem;
}

void memfile_reset(memfile_t mf) {
  mf->used = 0;
}

memfile_t memfile_open(size_t min_block_size) {
  struct memfile *mf = calloc(1, sizeof(*mf));
  if(min_block_size == 0)
    min_block_size = 1024;
  if((mf->mem = malloc(min_block_size))) {
    mf->allocated = min_block_size;
    return mf;
  }
  free(mf);
  return NULL;
}

size_t memfile_write(const void * restrict p, size_t n, size_t size, void * restrict mfp) {
  memfile_t mf = mfp;
  size_t length_to_write = n * size;
  size_t total_required_size = mf->used + length_to_write;
  // to do: check overflow
  while(total_required_size > mf->allocated) {
    size_t new_size = mf->allocated * 2;
    void *newmem = realloc(mf->mem, new_size);
    if(newmem) {
      mf->mem = newmem;
      mf->allocated = new_size;
    } else {
      errno = ENOMEM;
      return 0;
    }
  }
  memcpy((char *)mf->mem + mf->used, p, length_to_write);
  mf->used += length_to_write;
  return length_to_write;
}


int memfile_close(memfile_t mf) {
  if(mf) {
    free(mf->mem);
    free(mf);
    return 0;
  }
  return 1;
}

