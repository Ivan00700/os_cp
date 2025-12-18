#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include "allocator.h"

// Buddy (power-of-two) allocator working fully in-place on a provided memory region.

bool buddy_allocator_init(allocator_t* alloc, void* region, size_t region_size);
void buddy_allocator_deinit(allocator_t* alloc);

void* buddy_allocator_alloc(allocator_t* alloc, size_t size);
void buddy_allocator_free(allocator_t* alloc, void* ptr);

#endif
