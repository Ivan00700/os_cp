#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include "allocator.h"

// Аллокатор Бадди (блоки размера 2^n), полностью работающий «на месте» на предоставленном участке памяти

bool buddy_allocator_init(allocator_t* alloc, void* region, size_t region_size);
void buddy_allocator_deinit(allocator_t* alloc);

void* buddy_allocator_alloc(allocator_t* alloc, size_t size);
void buddy_allocator_free(allocator_t* alloc, void* ptr);

#endif
