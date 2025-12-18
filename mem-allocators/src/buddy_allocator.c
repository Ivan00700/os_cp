#include "../include/buddy_allocator.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define BUDDY_MAGIC 0xC0FFEE42u
#define BUDDY_MAX_ORDERS 32
#define BUDDY_ALIGN 16

typedef struct buddy_free_block {
    struct buddy_free_block* next;
} buddy_free_block_t;

typedef struct {
    uint32_t magic;
    uint8_t order;
    uint8_t _pad[3];
    size_t requested_size;
} buddy_block_header_t;

typedef struct {
    void* base;
    size_t heap_size;
    uint8_t min_order;
    uint8_t max_order;
    buddy_free_block_t* free_lists[BUDDY_MAX_ORDERS];
} buddy_impl_t;

static void* align_up(void* p, size_t alignment) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

static size_t floor_log2_size(size_t x) {
    size_t r = 0;
    while (x >>= 1) {
        r++;
    }
    return r;
}

static size_t ceil_log2_size(size_t x) {
    if (x <= 1) return 0;
    size_t p = floor_log2_size(x - 1) + 1;
    return p;
}

static size_t order_to_size(uint8_t order) {
    return (size_t)1u << order;
}

static uint8_t compute_min_order(void) {
    size_t need = sizeof(buddy_free_block_t);
    if (sizeof(buddy_block_header_t) > need) {
        need = sizeof(buddy_block_header_t);
    }

    uint8_t order = 0;
    size_t v = 1;
    while (v < need) {
        v <<= 1;
        order++;
    }
    if (order < 5) {
        order = 5; // at least 32 bytes blocks
    }
    return order;
}

static buddy_free_block_t* pop_free(buddy_impl_t* impl, uint8_t order) {
    buddy_free_block_t* blk = impl->free_lists[order];
    if (blk) {
        impl->free_lists[order] = blk->next;
        blk->next = NULL;
    }
    return blk;
}

static void push_free(buddy_impl_t* impl, uint8_t order, void* block) {
    buddy_free_block_t* blk = (buddy_free_block_t*)block;
    blk->next = impl->free_lists[order];
    impl->free_lists[order] = blk;
}

static void* try_remove_buddy_from_list(buddy_impl_t* impl, uint8_t order, void* buddy) {
    buddy_free_block_t** prev = &impl->free_lists[order];
    buddy_free_block_t* cur = impl->free_lists[order];
    while (cur) {
        if ((void*)cur == buddy) {
            *prev = cur->next;
            cur->next = NULL;
            return buddy;
        }
        prev = &cur->next;
        cur = cur->next;
    }
    return NULL;
}

bool buddy_allocator_init(allocator_t* alloc, void* region, size_t region_size) {
    if (!alloc || !region || region_size < sizeof(buddy_impl_t) + 256) {
        return false;
    }

    void* impl_base = align_up(region, BUDDY_ALIGN);
    size_t impl_prefix = (size_t)((char*)impl_base - (char*)region);
    if (impl_prefix >= region_size) {
        return false;
    }

    size_t usable = region_size - impl_prefix;
    if (usable < sizeof(buddy_impl_t) + 256) {
        return false;
    }

    buddy_impl_t* impl = (buddy_impl_t*)impl_base;
    memset(impl, 0, sizeof(*impl));

    impl->min_order = compute_min_order();

    void* after_impl = (char*)impl_base + sizeof(buddy_impl_t);
    after_impl = align_up(after_impl, BUDDY_ALIGN);
    size_t after_prefix = (size_t)((char*)after_impl - (char*)impl_base);
    if (after_prefix >= usable) {
        return false;
    }

    void* region_end = (char*)impl_base + usable;
    size_t available = (size_t)((char*)region_end - (char*)after_impl);
    if (available < order_to_size(impl->min_order)) {
        return false;
    }

    size_t max_order = floor_log2_size(available);
    if (max_order >= BUDDY_MAX_ORDERS) {
        max_order = BUDDY_MAX_ORDERS - 1;
    }

    while (max_order > impl->min_order) {
        size_t blk_size = order_to_size((uint8_t)max_order);
        void* base = align_up(after_impl, blk_size);
        if ((char*)base + blk_size <= (char*)region_end) {
            impl->base = base;
            impl->heap_size = blk_size;
            impl->max_order = (uint8_t)max_order;
            break;
        }
        max_order--;
    }

    if (!impl->base || impl->heap_size == 0) {
        // Fallback to minimal order if needed
        size_t blk_size = order_to_size(impl->min_order);
        void* base = align_up(after_impl, blk_size);
        if ((char*)base + blk_size > (char*)region_end) {
            return false;
        }
        impl->base = base;
        impl->heap_size = blk_size;
        impl->max_order = impl->min_order;
    }

    for (int i = 0; i < BUDDY_MAX_ORDERS; i++) {
        impl->free_lists[i] = NULL;
    }

    push_free(impl, impl->max_order, impl->base);

    allocator_set_impl(alloc, impl);
    alloc->stats.heap_size = impl->heap_size;
    return true;
}

void buddy_allocator_deinit(allocator_t* alloc) {
    (void)alloc;
}

void* buddy_allocator_alloc(allocator_t* alloc, size_t size) {
    if (!alloc || size == 0) {
        return NULL;
    }

    buddy_impl_t* impl = (buddy_impl_t*)allocator_get_impl(alloc);
    if (!impl) {
        return NULL;
    }

    size_t need = size + sizeof(buddy_block_header_t);
    size_t order = ceil_log2_size(need);
    if (order < impl->min_order) {
        order = impl->min_order;
    }
    if (order > impl->max_order) {
        alloc->stats.failed_allocations++;
        return NULL;
    }

    uint8_t found = (uint8_t)order;
    while (found <= impl->max_order && impl->free_lists[found] == NULL) {
        found++;
    }
    if (found > impl->max_order) {
        alloc->stats.failed_allocations++;
        return NULL;
    }

    void* block = pop_free(impl, found);
    while (found > order) {
        found--;
        size_t half = order_to_size(found);
        void* buddy = (char*)block + half;
        push_free(impl, found, buddy);
    }

    buddy_block_header_t* hdr = (buddy_block_header_t*)block;
    hdr->magic = BUDDY_MAGIC;
    hdr->order = (uint8_t)order;
    hdr->requested_size = size;

    size_t committed = order_to_size((uint8_t)order);
    alloc->stats.total_allocations++;
    alloc->stats.current_allocated += committed;
    if (alloc->stats.current_allocated > alloc->stats.peak_allocated) {
        alloc->stats.peak_allocated = alloc->stats.current_allocated;
    }
    alloc->stats.current_requested += size;
    if (alloc->stats.current_requested > alloc->stats.peak_requested) {
        alloc->stats.peak_requested = alloc->stats.current_requested;
    }

    return (char*)block + sizeof(buddy_block_header_t);
}

void buddy_allocator_free(allocator_t* alloc, void* ptr) {
    if (!alloc || !ptr) {
        return;
    }

    buddy_impl_t* impl = (buddy_impl_t*)allocator_get_impl(alloc);
    if (!impl) {
        return;
    }

    buddy_block_header_t* hdr = (buddy_block_header_t*)((char*)ptr - sizeof(buddy_block_header_t));
    if (hdr->magic != BUDDY_MAGIC) {
        fprintf(stderr, "Error: Invalid pointer or corrupted block\n");
        return;
    }

    uint8_t order = hdr->order;
    if (order < impl->min_order || order > impl->max_order) {
        fprintf(stderr, "Error: Invalid block order\n");
        return;
    }

    void* block = (void*)hdr;

    // Update stats first.
    size_t committed = order_to_size(order);
    alloc->stats.total_frees++;
    alloc->stats.current_allocated -= committed;
    alloc->stats.current_requested -= hdr->requested_size;

    uintptr_t base = (uintptr_t)impl->base;
    uintptr_t b = (uintptr_t)block;
    if (b < base || b >= base + impl->heap_size) {
        fprintf(stderr, "Error: Pointer out of allocator range\n");
        return;
    }

    // Coalesce with buddy if available.
    while (order < impl->max_order) {
        size_t sz = order_to_size(order);
        uintptr_t offset = b - base;
        uintptr_t buddy_off = offset ^ (uintptr_t)sz;
        void* buddy = (void*)(base + buddy_off);

        if (!try_remove_buddy_from_list(impl, order, buddy)) {
            break;
        }

        // Merge: pick the lower address.
        if ((uintptr_t)buddy < b) {
            b = (uintptr_t)buddy;
        }
        order++;
    }

    push_free(impl, order, (void*)b);
}
