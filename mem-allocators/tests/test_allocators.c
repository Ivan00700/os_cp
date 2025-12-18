#include "../include/allocator.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define TEST_HEAP_SIZE (1024 * 1024)  /* 1 MB */

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("Running test: %s...", name); \
        fflush(stdout); \
    } while(0)

#define TEST_PASS() \
    do { \
        printf(" PASS\n"); \
        tests_passed++; \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        printf(" FAIL: %s\n", msg); \
        tests_failed++; \
    } while(0)

#define ASSERT(condition, msg) \
    do { \
        if (!(condition)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while(0)

/* Test basic allocation and deallocation */
void test_basic_alloc_free(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptr = allocator_alloc(alloc, 100);
    ASSERT(ptr != NULL, "Failed to allocate memory");
    
    memset(ptr, 0xAA, 100);  /* Write to memory to test it works */
    
    allocator_free(alloc, ptr);
    allocator_destroy(alloc);

    free(backing);
    
    TEST_PASS();
}

/* Test multiple allocations */
void test_multiple_allocs(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = allocator_alloc(alloc, 50 + i * 10);
        ASSERT(ptrs[i] != NULL, "Failed to allocate memory");
        memset(ptrs[i], i, 50 + i * 10);
    }
    
    for (int i = 0; i < 10; i++) {
        allocator_free(alloc, ptrs[i]);
    }
    
    allocator_destroy(alloc);
    free(backing);
    TEST_PASS();
}

/* Test allocation of different sizes */
void test_varied_sizes(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    void* ptrs[8];
    
    for (int i = 0; i < 8; i++) {
        ptrs[i] = allocator_alloc(alloc, sizes[i]);
        ASSERT(ptrs[i] != NULL, "Failed to allocate memory");
    }
    
    for (int i = 0; i < 8; i++) {
        allocator_free(alloc, ptrs[i]);
    }
    
    allocator_destroy(alloc);
    free(backing);
    TEST_PASS();
}

/* Test reuse of freed memory */
void test_memory_reuse(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptr1 = allocator_alloc(alloc, 100);
    ASSERT(ptr1 != NULL, "Failed to allocate memory");
    
    allocator_free(alloc, ptr1);
    
    void* ptr2 = allocator_alloc(alloc, 100);
    ASSERT(ptr2 != NULL, "Failed to reuse memory");
    
    allocator_free(alloc, ptr2);
    allocator_destroy(alloc);
    free(backing);
    TEST_PASS();
}

/* Test allocation patterns */
void test_alloc_pattern(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    /* Allocate, free, allocate pattern */
    for (int i = 0; i < 5; i++) {
        void* ptr = allocator_alloc(alloc, 200);
        ASSERT(ptr != NULL, "Failed to allocate in pattern");
        memset(ptr, i, 200);
        allocator_free(alloc, ptr);
    }
    
    allocator_destroy(alloc);
    free(backing);
    TEST_PASS();
}

/* Test edge cases */
void test_edge_cases(allocator_type_t type, const char* name) {
    TEST(name);

    void* backing = malloc(TEST_HEAP_SIZE + 64);
    ASSERT(backing != NULL, "Failed to allocate backing memory");
    allocator_t* alloc = allocator_create(type, backing, TEST_HEAP_SIZE);
    ASSERT(alloc != NULL, "Failed to create allocator");
    
    /* Allocate size 0 should return NULL */
    void* ptr = allocator_alloc(alloc, 0);
    ASSERT(ptr == NULL, "Allocating 0 bytes should return NULL");
    
    /* Free NULL should not crash */
    allocator_free(alloc, NULL);
    
    allocator_destroy(alloc);
    free(backing);
    TEST_PASS();
}

int main(void) {
    printf("=== Memory Allocator Unit Tests ===\n\n");
    
    printf("--- Segregated Free-List Allocator Tests ---\n");
    test_basic_alloc_free(ALLOCATOR_SEGREGATED_FREELIST, 
                          "Segregated: Basic alloc/free");
    test_multiple_allocs(ALLOCATOR_SEGREGATED_FREELIST, 
                        "Segregated: Multiple allocations");
    test_varied_sizes(ALLOCATOR_SEGREGATED_FREELIST, 
                     "Segregated: Varied sizes");
    test_memory_reuse(ALLOCATOR_SEGREGATED_FREELIST, 
                     "Segregated: Memory reuse");
    test_alloc_pattern(ALLOCATOR_SEGREGATED_FREELIST, 
                      "Segregated: Allocation patterns");
    test_edge_cases(ALLOCATOR_SEGREGATED_FREELIST, 
                   "Segregated: Edge cases");
    
    printf("\n--- Buddy Allocator Tests ---\n");
    test_basic_alloc_free(ALLOCATOR_BUDDY, 
                          "Buddy: Basic alloc/free");
    test_multiple_allocs(ALLOCATOR_BUDDY, 
                        "Buddy: Multiple allocations");
    test_varied_sizes(ALLOCATOR_BUDDY, 
                     "Buddy: Varied sizes");
    test_memory_reuse(ALLOCATOR_BUDDY, 
                     "Buddy: Memory reuse");
    test_alloc_pattern(ALLOCATOR_BUDDY, 
                      "Buddy: Allocation patterns");
    test_edge_cases(ALLOCATOR_BUDDY, 
                   "Buddy: Edge cases");
    
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
