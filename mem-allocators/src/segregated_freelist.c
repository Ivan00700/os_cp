#include "../include/segregated_freelist.h"
#include "../include/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

const size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

typedef struct free_block {
    struct free_block* next;
    size_t size;
} free_block_t;

typedef struct {
    size_t committed_size;
    size_t requested_size;
    uint32_t magic;
} block_header_t;

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN_SIZE 8
#define HEADER_SIZE sizeof(block_header_t)

typedef struct {
    void* heap;
    size_t heap_size;
    free_block_t* free_lists[NUM_SIZE_CLASSES]; // свободные блоки для каждого класса
    free_block_t* large_blocks; // доп блоки
} segregated_freelist_allocator_t;

static void* align_up(void* p, size_t alignment) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

static int get_size_class(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return -1;
}

static size_t align_size(size_t size) {
    return (size + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);
}

bool segregated_freelist_init(allocator_t* alloc, void* region, size_t region_size) {
    if (!alloc || !region || region_size < sizeof(segregated_freelist_allocator_t) + SIZE_CLASSES[0]) {
        return false;
    }

    void* impl_base = align_up(region, ALIGN_SIZE);
    size_t impl_prefix = (size_t)((char*)impl_base - (char*)region);
    if (impl_prefix >= region_size) {
        return false;
    }
    size_t usable = region_size - impl_prefix;
    if (usable < sizeof(segregated_freelist_allocator_t) + SIZE_CLASSES[0]) {
        return false;
    }

    segregated_freelist_allocator_t* sf_alloc = (segregated_freelist_allocator_t*)impl_base;
    memset(sf_alloc, 0, sizeof(*sf_alloc));

    void* heap_start = (char*)impl_base + sizeof(segregated_freelist_allocator_t);
    heap_start = align_up(heap_start, ALIGN_SIZE);
    size_t heap_prefix = (size_t)((char*)heap_start - (char*)impl_base);
    if (heap_prefix >= usable) {
        return false;
    }

    sf_alloc->heap = heap_start;
    sf_alloc->heap_size = usable - heap_prefix;

    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        sf_alloc->free_lists[i] = NULL;
    }
    sf_alloc->large_blocks = (free_block_t*)sf_alloc->heap;
    sf_alloc->large_blocks->next = NULL;
    sf_alloc->large_blocks->size = sf_alloc->heap_size;

    // Store impl pointer in the generic allocator.
    allocator_set_impl(alloc, sf_alloc);
    // heap_size for utilization factor should refer to the actual usable heap.
    alloc->stats.heap_size = sf_alloc->heap_size;
    return true;
}

void segregated_freelist_deinit(allocator_t* alloc) {
    (void)alloc;
}

void* segregated_freelist_alloc(allocator_t* alloc, size_t size) {
    if (!alloc || size == 0) {
        return NULL;
    }

    segregated_freelist_allocator_t* sf_alloc = (segregated_freelist_allocator_t*)allocator_get_impl(alloc);
    if (!sf_alloc) {
        return NULL;
    }

    size_t total_size = align_size(size + HEADER_SIZE);
    int class_idx = get_size_class(total_size);
    
    free_block_t* block = NULL;
    
    if (class_idx >= 0) {
        if (sf_alloc->free_lists[class_idx]) {
            block = sf_alloc->free_lists[class_idx];
            sf_alloc->free_lists[class_idx] = block->next;
        } else {
            free_block_t** prev_ptr = &sf_alloc->large_blocks;
            free_block_t* curr = sf_alloc->large_blocks;
            
            while (curr) {
                if (curr->size >= SIZE_CLASSES[class_idx]) {
                    *prev_ptr = curr->next;
                    
                    size_t block_size = SIZE_CLASSES[class_idx];
                    size_t remaining = curr->size - block_size;
                    
                    block = curr;
                    
                    if (remaining >= SIZE_CLASSES[0]) {
                        free_block_t* remainder = (free_block_t*)((char*)curr + block_size);
                        remainder->size = remaining;
                        remainder->next = sf_alloc->large_blocks;
                        sf_alloc->large_blocks = remainder;
                    }
                    
                    break;
                }
                prev_ptr = &curr->next;
                curr = curr->next;
            }
        }
        
        if (block) {
            block_header_t* header = (block_header_t*)block;
            header->committed_size = total_size;
            header->requested_size = size;
            header->magic = BLOCK_MAGIC;

            alloc->stats.total_allocations++;
            alloc->stats.current_allocated += total_size;
            if (alloc->stats.current_allocated > alloc->stats.peak_allocated) {
                alloc->stats.peak_allocated = alloc->stats.current_allocated;
            }
            alloc->stats.current_requested += size;
            if (alloc->stats.current_requested > alloc->stats.peak_requested) {
                alloc->stats.peak_requested = alloc->stats.current_requested;
            }
            
            return (char*)block + HEADER_SIZE;
        }
    } else {
        free_block_t** prev_ptr = &sf_alloc->large_blocks;
        free_block_t* curr = sf_alloc->large_blocks;
        
        while (curr) {
            if (curr->size >= total_size) {
                *prev_ptr = curr->next;
                
                size_t remaining = curr->size - total_size;
                block = curr;
                
                if (remaining >= SIZE_CLASSES[0]) {
                    free_block_t* remainder = (free_block_t*)((char*)curr + total_size);
                    remainder->size = remaining;
                    remainder->next = sf_alloc->large_blocks;
                    sf_alloc->large_blocks = remainder;
                }
                
                block_header_t* header = (block_header_t*)block;
                header->committed_size = total_size;
                header->requested_size = size;
                header->magic = BLOCK_MAGIC;

                alloc->stats.total_allocations++;
                alloc->stats.current_allocated += total_size;
                if (alloc->stats.current_allocated > alloc->stats.peak_allocated) {
                    alloc->stats.peak_allocated = alloc->stats.current_allocated;
                }
                alloc->stats.current_requested += size;
                if (alloc->stats.current_requested > alloc->stats.peak_requested) {
                    alloc->stats.peak_requested = alloc->stats.current_requested;
                }
                
                return (char*)block + HEADER_SIZE;
            }
            prev_ptr = &curr->next;
            curr = curr->next;
        }
    }
    
    alloc->stats.failed_allocations++;
    return NULL;
}

void segregated_freelist_free(allocator_t* alloc, void* ptr) {
    if (!alloc || !ptr) {
        return;
    }
    
    segregated_freelist_allocator_t* sf_alloc = (segregated_freelist_allocator_t*)allocator_get_impl(alloc);
    if (!sf_alloc) {
        return;
    }
    block_header_t* header = (block_header_t*)((char*)ptr - HEADER_SIZE);
    
    if (header->magic != BLOCK_MAGIC) {
        fprintf(stderr, "Error: Invalid pointer or corrupted block\n");
        return;
    }
    
    size_t total_size = header->committed_size;
    alloc->stats.total_frees++;
    alloc->stats.current_allocated -= total_size;
    alloc->stats.current_requested -= header->requested_size;
    
    int class_idx = get_size_class(total_size);
    free_block_t* block = (free_block_t*)header;
    block->size = total_size;
    
    if (class_idx >= 0 && total_size == SIZE_CLASSES[class_idx]) {
        block->next = sf_alloc->free_lists[class_idx];
        sf_alloc->free_lists[class_idx] = block;
    } else {
        block->next = sf_alloc->large_blocks;
        sf_alloc->large_blocks = block;
    }
}
