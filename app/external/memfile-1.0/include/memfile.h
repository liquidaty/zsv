#ifndef MEMFILE_H
#define MEMFILE_H

typedef struct memfile * memfile_t;

memfile_t memfile_open(size_t min_block_size);
off_t memfile_tell(memfile_t);
size_t memfile_write(const void * restrict p, size_t n, size_t size, void * restrict mf);
int memfile_close(memfile_t);

// additional functions beyond standard FILE api
void *memfile_data(memfile_t);
void memfile_reset(memfile_t mf); // start next write from position 0

#endif
