#ifndef HEAP_DRIVER_H
#define HEAP_DRIVER_H

#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t, uintptr_t

void  heap_init(void);            // clear the block map (all blocks free)
void *heap_alloc(size_t size);    // get 'size' bytes, returns NULL if no space
void  heap_free(void *ptr);       // give the blocks back

#endif