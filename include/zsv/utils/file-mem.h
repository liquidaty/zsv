#ifndef ZSV_FILEMEM_H_H
#define ZSV_FILEMEM_H_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct zsv_filemem zsv_filemem;

// Public API Functions
zsv_filemem *zsv_filemem_open(size_t buffsz);
size_t zsv_filemem_write(const void *data, size_t sz, size_t n, zsv_filemem *zfm);
int zsv_filemem_rewind(zsv_filemem *zfm);
size_t zsv_filemem_read(void *buffer, size_t size, size_t nitems, zsv_filemem *zfm);
void zsv_filemem_close(zsv_filemem *zfm);

#endif // ZSV_FILEMEM_H_H
