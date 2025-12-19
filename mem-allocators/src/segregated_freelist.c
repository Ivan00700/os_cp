#include "../include/segregated_freelist.h"
#include "../include/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/*
 * Segregated Free-List аллокатор (раздельные списки свободных блоков)
 *
 * Ключевая идея:
 * - Для небольших размеров есть фиксированные «классы размеров» (SIZE_CLASSES)
 * - Для каждого класса поддерживается отдельный список свободных блоков free_lists[i]
 * - Для всего остального (и как «резервный источник» для пополнения классов)
 *   используется список large_blocks — список более крупных свободных участков
 *
 * Как выделяется память:
 * - Пользователь просит size байт
 * - Аллокатор добавляет служебный заголовок block_header_t и выравнивает размер
 * - Затем выбирает ближайший подходящий класс SIZE_CLASSES[i] (если есть)
 * - Если в списке класса нет готового блока, берёт подходящий кусок из large_blocks,
 *   отрезает нужный размер и остаток (если достаточно большой) возвращает обратно в large_blocks
 *
 * Как освобождается память:
 * - По указателю на полезную нагрузку (payload) находим заголовок блока
 * - Проверяем magic (простая защита от чужих указателей/повреждений)
 * - Возвращаем блок либо в список соответствующего класса (если размер точно равен классу),
 *   либо в large_blocks
 *
 * Важно:
 * - В этой реализации НЕТ слияния соседних свободных блоков (coalescing), поэтому
 *   со временем возможна фрагментация
 * - Аллокатор работает полностью in-place: все структуры и куча лежат в region,
 *   переданном через allocator_create(...)
 */

// Размерные классы для списков свободных блоков
// Пример: если попросили 50 байт полезной нагрузки, после учёта заголовка/выравнивания
// аллокатор может выделить блок класса 64 (и разницу считать внутренней фрагментацией)
const size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

typedef struct free_block {
    // Представление свободного блока в списках
    // Память свободного блока используется для хранения next и size
    struct free_block* next;
    size_t size;
} free_block_t;

typedef struct {
    // Заголовок «занятого» блока
    // committed_size — фактически занятый размер внутри кучи (включая заголовок и выравнивание)
    // requested_size — размер, который запросил пользователь (только полезная нагрузка)
    size_t committed_size;
    size_t requested_size;
    uint32_t magic;
} block_header_t;

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN_SIZE 8
#define HEADER_SIZE sizeof(block_header_t)

// моя аналогия чтобы проще запомнить - free_lists - кусочки колбасы разной длины, 
// а large_blocks - целая колбаса от которой мы отрезаем куски если нам потребовался какой то огромный кусман
typedef struct {
    void* heap;
    size_t heap_size;
    // Списки свободных блоков по классам размеров
    free_block_t* free_lists[NUM_SIZE_CLASSES];
    // Список более крупных свободных кусков (используется как источник для нарезки)
    free_block_t* large_blocks;
} segregated_freelist_allocator_t;

static void* align_up(void* p, size_t alignment) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

static int get_size_class(size_t size) {
    // Находим минимальный класс, который способен вместить size
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return -1;
}

static size_t align_size(size_t size) {
    // Округляем вверх до ALIGN_SIZE
    return (size + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);
}

bool segregated_freelist_init(allocator_t* alloc, void* region, size_t region_size) {
    // В region должны поместиться:
    // - структура состояния segregated_freelist_allocator_t,
    // - и хотя бы один минимальный блок (SIZE_CLASSES[0]) для работы аллокатора
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

    // Изначально вся куча — один большой свободный блок в large_blocks
    sf_alloc->large_blocks = (free_block_t*)sf_alloc->heap;
    sf_alloc->large_blocks->next = NULL;
    sf_alloc->large_blocks->size = sf_alloc->heap_size;

    // Сохраняем указатель на реализацию в «универсальном» аллокаторе
    allocator_set_impl(alloc, sf_alloc);
    // heap_size для коэффициента утилизации должен соответствовать реально доступной куче
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

    // total_size — сколько байт реально займёт блок в куче с учётом заголовка и выравнивания
    size_t total_size = align_size(size + HEADER_SIZE);
    int class_idx = get_size_class(total_size); // индекс размерного класса или -1
    
    free_block_t* block = NULL;
    
    if (class_idx >= 0) {
        // Случай 1: запрос помещается в один из размерных классов
        // Сначала пытаемся взять готовый блок из free_lists[class_idx]
        if (sf_alloc->free_lists[class_idx]) {
            block = sf_alloc->free_lists[class_idx];
            sf_alloc->free_lists[class_idx] = block->next;
        } else {
            // Если в списке класса пусто — берём блок из large_blocks и «нарезаем»
            free_block_t** prev_ptr = &sf_alloc->large_blocks;
            free_block_t* curr = sf_alloc->large_blocks;
            
            while (curr) {
                if (curr->size >= SIZE_CLASSES[class_idx]) {
                    *prev_ptr = curr->next;
                    
                    size_t block_size = SIZE_CLASSES[class_idx];
                    size_t remaining = curr->size - block_size;
                    
                    block = curr;
                    
                    // Если остаток достаточно велик — возвращаем его как новый free_block в large_blocks
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
            // Помечаем блок как занятый: пишем заголовок и возвращаем payload
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
        // Случай 2: запрос больше максимального размерного класса
        // Ищем подходящий кусок в large_blocks (простая стратегия: первый подходящий)
        free_block_t** prev_ptr = &sf_alloc->large_blocks;
        free_block_t* curr = sf_alloc->large_blocks;
        
        while (curr) {
            if (curr->size >= total_size) {
                *prev_ptr = curr->next;
                
                size_t remaining = curr->size - total_size;
                block = curr;
                
                // Возвращаем остаток обратно в large_blocks, если он имеет смысл
                if (remaining >= SIZE_CLASSES[0]) {
                    free_block_t* remainder = (free_block_t*)((char*)curr + total_size);
                    remainder->size = remaining;
                    remainder->next = sf_alloc->large_blocks;
                    sf_alloc->large_blocks = remainder;
                }
                
                // Пишем заголовок и возвращаем полезную нагрузку
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
    
    // Базовая проверка корректности указателя
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
    
    // Если размер ровно равен одному из классов — возвращаем в соответствующий список
    // Иначе отправляем в large_blocks
    if (class_idx >= 0 && total_size == SIZE_CLASSES[class_idx]) {
        block->next = sf_alloc->free_lists[class_idx];
        sf_alloc->free_lists[class_idx] = block;
    } else {
        block->next = sf_alloc->large_blocks;
        sf_alloc->large_blocks = block;
    }
}
