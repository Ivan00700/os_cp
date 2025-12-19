#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdbool.h>

/*
 * Единый интерфейс аллокаторов памяти для проекта
 *
 * Идея:
 * - allocator_create(...) создаёт «универсальный» объект allocator_t внутри
 *   предоставленного пользователем участка памяти (in-place)
 * - конкретный алгоритм (segregated free list или buddy) инициализируется
 *   на оставшейся части этого же участка памяти
 * - allocator_alloc/allocator_free делегируют работу выбранной реализации
 *   через набор function-pointer'ов
 *
 * Важно про владение памятью:
 * - allocator_create НЕ владеет realMemory и НЕ освобождает его
 * - allocator_create_with_malloc выделяет realMemory сам и освобождает его
 *   при allocator_destroy
 *
 * Ограничения:
 * - интерфейс не потокобезопасен
 * - allocator_realloc здесь реализован как упрощённая заглушка (без memcpy),
 *   т.к. общий слой не хранит метаданные о размере блока
 */

typedef enum {
    // Алгоритмы по варианту:
    // 1) Списки свободных блоков (раздельные списки по классам размеров)
    // 2) Блоки размера 2^n (аллокатор Бадди)
    ALLOCATOR_SEGREGATED_FREELIST,
    ALLOCATOR_BUDDY
} allocator_type_t;

typedef struct allocator allocator_t;

typedef void (*allocator_destroy_impl_fn)(allocator_t* alloc);
typedef void* (*allocator_alloc_impl_fn)(allocator_t* alloc, size_t size);
typedef void (*allocator_free_impl_fn)(allocator_t* alloc, void* ptr);

// Создаёт аллокатор «на месте» (внутри предоставленного участка памяти)
//
// Параметры:
// - realMemory / memory_size: участок памяти, внутри которого будет размещён:
// - realMemory / memory_size: участок памяти, внутри которого будет размещён:
//   1) заголовок allocator_t (управляющая структура),
//   2) затем «регион реализации» (impl_region) для конкретного алгоритма
//
// Возвращает:
// - указатель на allocator_t, расположенный внутри realMemory, либо NULL при ошибке
//
// Владение:
// - Аллокатор НЕ владеет `realMemory` и не освобождает его
allocator_t* allocator_create(allocator_type_t type, void* realMemory, size_t memory_size);

// Упрощённый конструктор: сам выделяет память под регион через malloc()
// В этом режиме аллокатор владеет регионом и освобождает его в allocator_destroy()
allocator_t* allocator_create_with_malloc(allocator_type_t type, size_t memory_size);

void allocator_destroy(allocator_t* alloc);

void* allocator_alloc(allocator_t* alloc, size_t size);

void allocator_free(allocator_t* alloc, void* ptr);

void* allocator_realloc(allocator_t* alloc, void* ptr, size_t new_size);

typedef struct {
    size_t total_allocations;
    size_t total_frees;
    // Байты, «занятые» внутри управляемого региона
    // Сюда входит всё, что потребляет сам аллокатор: заголовки блоков,
    // выравнивание, округление до классов/степеней двойки и т.п
    size_t current_allocated;
    size_t peak_allocated;
    // Байты, запрошенные пользователем (только полезная нагрузка, без накладных расходов)
    size_t current_requested;
    size_t peak_requested;
    size_t failed_allocations;
    // Полный размер управляемого региона, доступного реализации аллокатора
    // Используется, например, для коэффициента утилизации: peak_requested / heap_size
    size_t heap_size;
} allocator_stats_t;

struct allocator {
    // Выбранный тип/алгоритм
    allocator_type_t type;

    // «Сырой» участок памяти, переданный пользователем (или выделенный через malloc())
    void* real_memory;
    size_t real_memory_size;

    // Подучасток real_memory, доступный конкретной реализации (после выравнивания и allocator_t)
    void* impl_region;
    size_t impl_region_size;

    // Указатель на внутреннее состояние реализации (struct ..._impl), лежащее в impl_region
    void* impl;

    // true, если real_memory выделена внутри allocator_create_with_malloc и должна быть освобождена
    bool owns_real_memory;

    // Таблица виртуальных методов: конкретная реализация заполняет эти указатели в create()
    allocator_destroy_impl_fn destroy_impl;
    allocator_alloc_impl_fn alloc_impl;
    allocator_free_impl_fn free_impl;

    // Сводная статистика (обновляется реализациями при alloc/free)
    allocator_stats_t stats;
};

void allocator_get_stats(allocator_t* alloc, allocator_stats_t* stats);

void allocator_reset_stats(allocator_t* alloc);

// Внутренние хелперы (используются реализациями алгоритмов)
// Нужны, чтобы общий слой мог хранить impl «как void*», не раскрывая структуру реализации
void allocator_set_impl(allocator_t* alloc, void* impl);
void* allocator_get_impl(allocator_t* alloc);

// --- Обёртки для варианта (как в задании) ---
// В тексте задания используются имена createMemoryAllocator/alloc/free
// Мы намеренно НЕ определяем глобальную функцию с именем `free`, чтобы избежать
// конфликта со стандартной библиотекой Си. Вместо этого используйте allocator_free()
typedef allocator_t Allocator;

// Опционально: в варианте может требоваться функция вида `free(Allocator*, void*)`
// Даём безопасное имя, которое возвращает NULL (чтобы можно было использовать в выражениях)
void* allocator_free_block(Allocator* allocator, void* block);

static inline Allocator* createMemoryAllocator(allocator_type_t type, void* realMemory, size_t memory_size) {
    return allocator_create(type, realMemory, memory_size);
}

static inline void* alloc(Allocator* allocator, size_t block_size) {
    return allocator_alloc(allocator, block_size);
}

#endif /* ALLOCATOR_H */
