#include "../include/buddy_allocator.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * Buddy-аллокатор (блоки размера 2^n)
 *
 * Ключевая идея:
 * - Управляемый регион памяти рассматривается как один большой блок размера 2^max_order
 * - Для каждого порядка (order) поддерживается список свободных блоков free_lists[order]
 *   Размер блока равен 2^order байт
 * - При выделении памяти мы берём первый доступный блок порядка >= нужного
 *   Если блок больше — рекурсивно делим его пополам, половинку возвращаем в свободный список
 * - При освобождении мы пытаемся слить блок с его «бадди» (парным блоком той же половины)
 *   Если бадди тоже свободен — удаляем бадди из списка и поднимаемся на порядок выше
 *
 * Важно про in-place:
 * - Структура состояния buddy_impl_t размещается внутри переданного region
 * - Куча (impl->base .. base+heap_size) также лежит в этом же region
 * - Никаких malloc/free внутри аллокатора (кроме сообщений об ошибках через stderr)
 *
 * Про заголовок блока:
 * - В начале каждого выделенного блока хранится buddy_block_header_t
 * - magic используется для проверки корректности указателя
 * - order нужен, чтобы при free знать размер блока и корректно сливать
 * - requested_size нужен для статистики (сколько запросил пользователь)
 */

#define BUDDY_MAGIC 0xC0FFEE42u
#define BUDDY_MAX_ORDERS 32
#define BUDDY_ALIGN 16

typedef struct buddy_free_block {
    // Представление свободного блока: используется как узел односвязного списка
    struct buddy_free_block* next;
} buddy_free_block_t;

typedef struct {
    // Заголовок выделенного блока. Лежит непосредственно в начале блока
    uint32_t magic; // для проверки а точно ли это наш указатель
    uint8_t order; // размер блока в виде порядка (2^order)
    uint8_t _pad[3];
    size_t requested_size; // сколько байт запросил пользователь
} buddy_block_header_t;

typedef struct {
    // base: начало управляемой кучи, выровненное по размеру самого большого блока
    // heap_size: размер кучи (строго степень двойки)
    void* base;
    size_t heap_size;
    // min_order: минимальный порядок, который способен вместить служебные структуры
    // max_order: порядок самого большого блока (order_to_size(max_order) == heap_size)
    uint8_t min_order;
    uint8_t max_order;
    // Списки свободных блоков по порядкам
    buddy_free_block_t* free_lists[BUDDY_MAX_ORDERS];
} buddy_impl_t;

static void* align_up(void* p, size_t alignment) {
    // Выравниваем адрес вверх до alignment (степени двойки)
    uintptr_t v = (uintptr_t)p;
    uintptr_t a = (uintptr_t)alignment;
    return (void*)((v + (a - 1)) & ~(a - 1));
}

static size_t floor_log2_size(size_t x) {
    // floor(log2(x)) для x>0
    size_t r = 0;
    while (x >>= 1) {
        r++;
    }
    return r;
}

static size_t ceil_log2_size(size_t x) {
    // ceil(log2(x)) для x>=1
    if (x <= 1) return 0;
    size_t p = floor_log2_size(x - 1) + 1;
    return p;
}

static size_t order_to_size(uint8_t order) {
    // Преобразуем порядок в размер блока: 2^order
    return (size_t)1u << order;
}

static uint8_t compute_min_order(void) {
    // Минимальный порядок должен быть таким, чтобы в блок поместились:
    // - узел free-list (buddy_free_block_t),
    // - или заголовок выделенного блока (buddy_block_header_t) — что больше
    // Дополнительно ограничиваем минимальный размер (>=32 байт) для практичности
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
        order = 5; // минимум 32-байтные блоки
    }
    return order;
}

static buddy_free_block_t* pop_free(buddy_impl_t* impl, uint8_t order) {
    // Снимаем первый свободный блок нужного порядка
    buddy_free_block_t* blk = impl->free_lists[order];
    if (blk) {
        impl->free_lists[order] = blk->next;
        blk->next = NULL;
    }
    return blk;
}

static void push_free(buddy_impl_t* impl, uint8_t order, void* block) {
    // Добавляем блок в начало списка свободных блоков данного порядка
    buddy_free_block_t* blk = (buddy_free_block_t*)block;
    blk->next = impl->free_lists[order];
    impl->free_lists[order] = blk;
}

static void* try_remove_buddy_from_list(buddy_impl_t* impl, uint8_t order, void* buddy) {
    // Пытаемся найти и удалить конкретный адрес buddy из списка free_lists[order]
    // Возвращает buddy при успехе, иначе NULL
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
    // Минимальная оценка: должно хватить на buddy_impl_t и хоть сколько-то места под кучу
    if (!alloc || !region || region_size < sizeof(buddy_impl_t) + 256) {
        return false;
    }

    // 1) Размещаем структуру buddy_impl_t внутри region с выравниванием
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

    // 2) Определяем минимальный порядок (минимальный размер блока)
    impl->min_order = compute_min_order();

    // 3) После buddy_impl_t начинается область, в которой мы пытаемся разместить кучу
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

    // 4) Выбираем максимально возможный порядок, чтобы heap_size была степенью двойки
    // и целиком помещалась в доступное место
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
        // Если подходящий большой блок не нашёлся — используем минимальный порядок
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

    // 5) Изначально вся куча — один свободный блок максимального порядка
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

    // need — сколько нужно реально в блоке (payload + заголовок)
    size_t need = size + sizeof(buddy_block_header_t);
    size_t order = ceil_log2_size(need);
    if (order < impl->min_order) {
        order = impl->min_order;
    }
    if (order > impl->max_order) {
        alloc->stats.failed_allocations++;
        return NULL;
    }

    // Ищем первый доступный свободный блок порядка >= order
    uint8_t found = (uint8_t)order;
    while (found <= impl->max_order && impl->free_lists[found] == NULL) {
        found++;
    }
    if (found > impl->max_order) {
        alloc->stats.failed_allocations++;
        return NULL;
    }

    // Берём блок найденного порядка и при необходимости делим, пока не получим нужный order
    void* block = pop_free(impl, found);
    while (found > order) {
        found--;
        size_t half = order_to_size(found);
        // Правая половина становится свободной и уходит в список found
        void* buddy = (char*)block + half;
        push_free(impl, found, buddy);
    }

    // Записываем заголовок, чтобы при free восстановить order и размер запроса
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
    // Проверяем, что указатель действительно относится к нашему аллокатору
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

    // Сначала обновляем статистику
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

    // Пытаемся слить блок с его бадди
    // Бадди вычисляется через XOR по биту размера текущего порядка:
    //   buddy_off = offset ^ size
    // где offset = block - base, size = 2^order
    while (order < impl->max_order) {
        // нахождение бадди через XOR
        size_t sz = order_to_size(order);
        uintptr_t offset = b - base;
        uintptr_t buddy_off = offset ^ (uintptr_t)sz;
        void* buddy = (void*)(base + buddy_off);

        if (!try_remove_buddy_from_list(impl, order, buddy)) {
            // Бадди не свободен — слияние на этом заканчивается
            break;
        }

        // Слияние: выбираем меньший адрес как базу объединённого блока
        if ((uintptr_t)buddy < b) {
            b = (uintptr_t)buddy;
        }
        order++;
    }

    push_free(impl, order, (void*)b);
}
