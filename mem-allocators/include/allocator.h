#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    // Variant-required algorithms:
    // 1) Free lists (segregated free list)
    // 2) Power-of-two blocks (buddy allocator)
    ALLOCATOR_SEGREGATED_FREELIST,
    ALLOCATOR_BUDDY
} allocator_type_t;

typedef struct allocator allocator_t;

typedef void (*allocator_destroy_impl_fn)(allocator_t* alloc);
typedef void* (*allocator_alloc_impl_fn)(allocator_t* alloc, size_t size);
typedef void (*allocator_free_impl_fn)(allocator_t* alloc, void* ptr);

// Creates an allocator *in-place* using the provided memory region.
// The allocator does not take ownership of realMemory.
allocator_t* allocator_create(allocator_type_t type, void* realMemory, size_t memory_size);

// Convenience constructor using malloc() for the backing region.
// The allocator will own and free that region in allocator_destroy().
allocator_t* allocator_create_with_malloc(allocator_type_t type, size_t memory_size);

void allocator_destroy(allocator_t* alloc);

void* allocator_alloc(allocator_t* alloc, size_t size);

void allocator_free(allocator_t* alloc, void* ptr);

void* allocator_realloc(allocator_t* alloc, void* ptr, size_t new_size);

typedef struct {
    size_t total_allocations;
    size_t total_frees;
    // Bytes reserved/consumed from the managed region (includes allocator overhead & rounding).
    size_t current_allocated;
    size_t peak_allocated;
    // Bytes requested by user (payload only).
    size_t current_requested;
    size_t peak_requested;
    size_t failed_allocations;
    // Total size of managed region (useful for utilization factor).
    size_t heap_size;
} allocator_stats_t;

struct allocator {
    allocator_type_t type;
    void* real_memory;
    size_t real_memory_size;
    void* impl_region;
    size_t impl_region_size;
    void* impl;

    bool owns_real_memory;

    allocator_destroy_impl_fn destroy_impl;
    allocator_alloc_impl_fn alloc_impl;
    allocator_free_impl_fn free_impl;

    allocator_stats_t stats;
};

void allocator_get_stats(allocator_t* alloc, allocator_stats_t* stats);

void allocator_reset_stats(allocator_t* alloc);

// Internal helpers (used by algorithm implementations)
void allocator_set_impl(allocator_t* alloc, void* impl);
void* allocator_get_impl(allocator_t* alloc);

// --- Course-variant convenience wrappers ---
// The assignment spec uses names createMemoryAllocator/alloc/free.
// We intentionally avoid defining a global function named `free` to prevent
// collisions with the C standard library. Use allocator_free() instead.
typedef allocator_t Allocator;

// Optional: course variant wants a function shaped like `free(Allocator*, void*)`.
// We provide a safe name that returns NULL (so it can be used in expressions).
void* allocator_free_block(Allocator* allocator, void* block);

static inline Allocator* createMemoryAllocator(allocator_type_t type, void* realMemory, size_t memory_size) {
    return allocator_create(type, realMemory, memory_size);
}

static inline void* alloc(Allocator* allocator, size_t block_size) {
    return allocator_alloc(allocator, block_size);
}

#endif /* ALLOCATOR_H */
