#include "../include/allocator.h"
#include "../include/segregated_freelist.h"
#include "../include/buddy_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ALLOCATOR_ALIGN 16

static void* align_up(void* p, size_t alignment) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

// allocator_t is defined in allocator.h

allocator_t* allocator_create_with_malloc(allocator_type_t type, size_t memory_size) {
    void* backing = malloc(memory_size + ALLOCATOR_ALIGN);
    if (!backing) {
        return NULL;
    }
    allocator_t* alloc = allocator_create(type, backing, memory_size);
    if (!alloc) {
        free(backing);
        return NULL;
    }
    alloc->owns_real_memory = true;
    return alloc;
}

void allocator_set_impl(allocator_t* alloc, void* impl) {
    if (!alloc) return;
    alloc->impl = impl;
}

void* allocator_get_impl(allocator_t* alloc) {
    if (!alloc) return NULL;
    return alloc->impl;
}

void* allocator_free_block(Allocator* allocator, void* block) {
    allocator_free(allocator, block);
    return NULL;
}

allocator_t* allocator_create(allocator_type_t type, void* realMemory, size_t memory_size) {
    if (!realMemory || memory_size < sizeof(allocator_t)) {
        return NULL;
    }

    void* base = align_up(realMemory, ALLOCATOR_ALIGN);
    size_t prefix = (size_t)((char*)base - (char*)realMemory);
    if (prefix >= memory_size) {
        return NULL;
    }
    size_t usable = memory_size - prefix;
    if (usable < sizeof(allocator_t)) {
        return NULL;
    }

    allocator_t* alloc = (allocator_t*)base;
    memset(alloc, 0, sizeof(*alloc));
    alloc->type = type;
    alloc->real_memory = realMemory;
    alloc->real_memory_size = memory_size;
    alloc->owns_real_memory = false;

    void* after_hdr = (char*)base + sizeof(allocator_t);
    after_hdr = align_up(after_hdr, ALLOCATOR_ALIGN);
    size_t after_hdr_prefix = (size_t)((char*)after_hdr - (char*)base);
    if (after_hdr_prefix > usable) {
        return NULL;
    }
    alloc->impl_region = after_hdr;
    alloc->impl_region_size = usable - after_hdr_prefix;

    alloc->stats.heap_size = alloc->impl_region_size;

    bool ok = false;
    switch (type) {
        case ALLOCATOR_SEGREGATED_FREELIST:
            ok = segregated_freelist_init(alloc, alloc->impl_region, alloc->impl_region_size);
            alloc->destroy_impl = segregated_freelist_deinit;
            alloc->alloc_impl = segregated_freelist_alloc;
            alloc->free_impl = segregated_freelist_free;
            break;
        case ALLOCATOR_BUDDY:
            ok = buddy_allocator_init(alloc, alloc->impl_region, alloc->impl_region_size);
            alloc->destroy_impl = buddy_allocator_deinit;
            alloc->alloc_impl = buddy_allocator_alloc;
            alloc->free_impl = buddy_allocator_free;
            break;
        default:
            ok = false;
            break;
    }

    if (!ok) {
        return NULL;
    }

    return alloc;
}

void allocator_destroy(allocator_t* alloc) {
    if (!alloc) return;

    if (alloc->destroy_impl) {
        alloc->destroy_impl(alloc);
    }

    if (alloc->owns_real_memory && alloc->real_memory) {
        free(alloc->real_memory);
    }
}

void* allocator_alloc(allocator_t* alloc, size_t size) {
    if (!alloc) return NULL;

    if (!alloc->alloc_impl) {
        return NULL;
    }
    return alloc->alloc_impl(alloc, size);
}

void allocator_free(allocator_t* alloc, void* ptr) {
    if (!alloc || !ptr) return;

    if (alloc->free_impl) {
        alloc->free_impl(alloc, ptr);
    }
}

void* allocator_realloc(allocator_t* alloc, void* ptr, size_t new_size) {
    if (!alloc) return NULL;
    
    if (ptr == NULL) {
        return allocator_alloc(alloc, new_size);
    }
    
    if (new_size == 0) {
        allocator_free(alloc, ptr);
        return NULL;
    }
    
    void* new_ptr = allocator_alloc(alloc, new_size);
    // NOTE: Without size metadata in the generic layer, we cannot safely memcpy.
    // This realloc is a functional stub kept for API completeness.
    if (new_ptr) {
        allocator_free(alloc, ptr);
    }
    
    return new_ptr;
}

void allocator_get_stats(allocator_t* alloc, allocator_stats_t* stats) {
    if (!alloc || !stats) return;

    *stats = alloc->stats;
}

void allocator_reset_stats(allocator_t* alloc) {
    if (!alloc) return;

    allocator_type_t type = alloc->type;
    size_t heap_size = alloc->stats.heap_size;
    memset(&alloc->stats, 0, sizeof(alloc->stats));
    alloc->type = type;
    alloc->stats.heap_size = heap_size;
}
