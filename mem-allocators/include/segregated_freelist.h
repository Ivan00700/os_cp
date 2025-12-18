#ifndef SEGREGATED_FREELIST_H
#define SEGREGATED_FREELIST_H

#include "allocator.h"

#define NUM_SIZE_CLASSES 8
extern const size_t SIZE_CLASSES[NUM_SIZE_CLASSES];

bool segregated_freelist_init(allocator_t* alloc, void* region, size_t region_size);
void segregated_freelist_deinit(allocator_t* alloc);
void* segregated_freelist_alloc(allocator_t* alloc, size_t size);
void segregated_freelist_free(allocator_t* alloc, void* ptr);

#endif
