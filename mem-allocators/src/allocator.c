#include "../include/allocator.h"
#include "../include/segregated_freelist.h"
#include "../include/buddy_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ALLOCATOR_ALIGN 16

// Выравнивает указатель вверх до alignment (степени двойки)
// Используется, чтобы allocator_t и внутренние структуры были корректно выровнены
// для большинства платформ/ABI
static void* align_up(void* p, size_t alignment) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

// allocator_t объявлен в allocator.h
// Здесь мы реализуем общий слой, который:
// - размещает allocator_t внутри предоставленного realMemory,
// - выделяет «хвост» под impl_region,
// - передаёт impl_region конкретной реализации

allocator_t* allocator_create_with_malloc(allocator_type_t type, size_t memory_size) {
    // Выделяем с запасом, т.к. allocator_create может сдвинуть base вверх до ALLOCATOR_ALIGN
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
    // Требуем размер управляющей структуры (с учётом выравнивания это проверяется ниже)
    if (!realMemory || memory_size < sizeof(allocator_t)) {
        return NULL;
    }

    // 1) Выравниваем начало региона и проверяем, что не вышли за границы
    void* base = align_up(realMemory, ALLOCATOR_ALIGN); // указатель на выровненную память
    size_t prefix = (size_t)((char*)base - (char*)realMemory);
    if (prefix >= memory_size) {
        return NULL;
    }
    size_t usable = memory_size - prefix;
    if (usable < sizeof(allocator_t)) {
        return NULL;
    }

    // 2) Размещаем allocator_t прямо внутри пользовательского буфера
    // alloc - указатель на структуру аллокатора который хранится в начале выровненного региона
    allocator_t* alloc = (allocator_t*)base; // размещает аллокатор на выровненной памяти (адрес - base)
    memset(alloc, 0, sizeof(*alloc)); // обнуляем память под аллокатор
    alloc->type = type;
    alloc->real_memory = realMemory;
    alloc->real_memory_size = memory_size;
    alloc->owns_real_memory = false;

    // 3) Формируем impl_region сразу после allocator_t, тоже с выравниванием
    // Именно этот подрегион передаётся конкретной реализации
    void* after_hdr = (char*)base + sizeof(allocator_t);
    after_hdr = align_up(after_hdr, ALLOCATOR_ALIGN);
    size_t after_hdr_prefix = (size_t)((char*)after_hdr - (char*)base);
    if (after_hdr_prefix > usable) {
        return NULL;
    }
    alloc->impl_region = after_hdr;
    alloc->impl_region_size = usable - after_hdr_prefix;

    // Для метрик утилизации считаем «кучей» именно то, что видит реализация
    alloc->stats.heap_size = alloc->impl_region_size;

    bool ok = false;
    switch (type) {
        case ALLOCATOR_SEGREGATED_FREELIST:
            // Реализация segregated free list использует impl_region под свои структуры и кучу
            ok = segregated_freelist_init(alloc, alloc->impl_region, alloc->impl_region_size);
            alloc->destroy_impl = segregated_freelist_deinit;
            alloc->alloc_impl = segregated_freelist_alloc;
            alloc->free_impl = segregated_freelist_free;
            break;
        case ALLOCATOR_BUDDY:
            // Реализация buddy (2^n) также полностью работает внутри impl_region
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
        // Если инициализация реализации не удалась, возвращаем NULL
        // Ничего дополнительно освобождать не нужно: realMemory принадлежит вызывающему коду
        return NULL;
    }

    return alloc;
}

void allocator_destroy(allocator_t* alloc) {
    if (!alloc) return;

    // Даём реализации шанс «разобрать» внутреннее состояние (обычно no-op для in-place)
    if (alloc->destroy_impl) {
        alloc->destroy_impl(alloc);
    }

    // Освобождаем real_memory только если она была выделена внутри create_with_malloc
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
    // ВАЖНО: без метаданных о размере в общем слое нельзя безопасно сделать memcpy
    // Поэтому realloc здесь — «перевыделить и освободить старое» без копирования данных
    // Этого достаточно для демонстрации API, но не эквивалентно стандартному realloc
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
