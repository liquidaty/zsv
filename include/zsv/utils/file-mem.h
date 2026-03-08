#ifndef ZSV_MEMFILE_H_H
#define ZSV_MEMFILE_H_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct zsv_memfile zsv_memfile;

// Public API Functions
zsv_memfile *zsv_memfile_open(size_t buffsz);
size_t zsv_memfile_write(const void *data, size_t sz, size_t n, zsv_memfile *zfm);
int zsv_memfile_rewind(zsv_memfile *zfm);
size_t zsv_memfile_read(void *buffer, size_t size, size_t nitems, zsv_memfile *zfm);
void zsv_memfile_close(zsv_memfile *zfm);

#endif // ZSV_FILEMEM_H_H
