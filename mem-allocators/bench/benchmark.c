#include "../include/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif

#define DEFAULT_HEAP_SIZE (10 * 1024 * 1024)  /* 10 MB */
#define MAX_ALLOCS 10000

/* Сценарии бенчмарка */
typedef enum {
    BENCH_SEQUENTIAL,
    BENCH_RANDOM,
    BENCH_MIXED,
    BENCH_STRESS
} benchmark_type_t;

/* Получить текущее время в микросекундах */
static double get_time_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int inited = 0;
    if (!inited) {
        QueryPerformanceFrequency(&freq);
        inited = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000000.0 / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
#endif
}

/* Результат бенчмарка */
typedef struct {
    const char* allocator_name;
    const char* benchmark_name;
    double alloc_time_us;
    double free_time_us;
    size_t alloc_ops;
    size_t free_ops;
    double alloc_ops_per_sec;
    double free_ops_per_sec;
    double peak_utilization;
} benchmark_result_t;

/* Печать заголовка CSV */
void print_csv_header(void) {
    printf("Allocator,Benchmark,AllocTime_us,FreeTime_us,AllocOps,FreeOps,AllocOpsPerSec,FreeOpsPerSec,PeakUtilization\n");
}

/* Печать результата бенчмарка в формате CSV */
void print_result_csv(const benchmark_result_t* result) {
    printf("%s,%s,%.2f,%.2f,%zu,%zu,%.2f,%.2f,%.6f\n",
           result->allocator_name,
           result->benchmark_name,
           result->alloc_time_us,
           result->free_time_us,
           result->alloc_ops,
           result->free_ops,
           result->alloc_ops_per_sec,
           result->free_ops_per_sec,
           result->peak_utilization);
}

// Бенчмарк: тестирует последовательное выделение и освобождение
// Какой аллокатор, его название, сколько операций, куда печатать
void benchmark_sequential(allocator_t* alloc, const char* alloc_name, size_t num_ops, FILE* output) {
    size_t n = num_ops;
    if (n > 100000) n = 100000; // ограничиваем размер массива указателей
    void** ptrs = calloc(n, sizeof(void*));
    if (!ptrs) return;

    allocator_reset_stats(alloc);
    double alloc_start = get_time_us();
    size_t alloc_ok = 0;
    for (size_t i = 0; i < n; i++) {
        void* p = allocator_alloc(alloc, 64);
        if (!p) break;
        ptrs[alloc_ok++] = p;
    }
    double alloc_end = get_time_us();

    double free_start = get_time_us();
    for (size_t i = 0; i < alloc_ok; i++) {
        allocator_free(alloc, ptrs[i]);
    }
    double free_end = get_time_us();

    allocator_stats_t st;
    allocator_get_stats(alloc, &st);
    double util = (st.heap_size == 0) ? 0.0 : ((double)st.peak_requested / (double)st.heap_size);

    double alloc_elapsed = alloc_end - alloc_start;
    double free_elapsed = free_end - free_start;

    benchmark_result_t result = {
        .allocator_name = alloc_name,
        .benchmark_name = "Sequential",
        .alloc_time_us = alloc_elapsed,
        .free_time_us = free_elapsed,
        .alloc_ops = alloc_ok,
        .free_ops = alloc_ok,
        .alloc_ops_per_sec = (alloc_elapsed <= 0) ? 0.0 : (alloc_ok / (alloc_elapsed / 1000000.0)),
        .free_ops_per_sec = (free_elapsed <= 0) ? 0.0 : (alloc_ok / (free_elapsed / 1000000.0)),
        .peak_utilization = util
    };

    if (output) {
        fprintf(output, "%s,%s,%.2f,%.2f,%zu,%zu,%.2f,%.2f,%.6f\n",
                result.allocator_name, result.benchmark_name,
                result.alloc_time_us, result.free_time_us,
                result.alloc_ops, result.free_ops,
                result.alloc_ops_per_sec, result.free_ops_per_sec,
                result.peak_utilization);
    } else {
        print_result_csv(&result);
    }

    free(ptrs);
}

/* Бенчмарк: тестирует в случайных условиях */
void benchmark_random(allocator_t* alloc, const char* alloc_name, size_t num_ops, FILE* output) {
    size_t cap = 2000;
    if (num_ops < cap) cap = num_ops;
    void** ptrs = calloc(cap, sizeof(void*));
    if (!ptrs) return;

    allocator_reset_stats(alloc);
    srand(42);

    // Фаза А: выделения случайных размеров
    double alloc_start = get_time_us();
    size_t alloc_ok = 0;
    for (size_t i = 0; i < cap; i++) {
        size_t size = 16 + (rand() % 2048);
        void* p = allocator_alloc(alloc, size);
        if (!p) break;
        ptrs[alloc_ok++] = p;
    }
    double alloc_end = get_time_us();

    // Перемешиваем указатели, чтобы приблизить случайный порядок освобождения
    for (size_t i = 0; i + 1 < alloc_ok; i++) {
        size_t j = i + (rand() % (alloc_ok - i));
        void* tmp = ptrs[i];
        ptrs[i] = ptrs[j];
        ptrs[j] = tmp;
    }

    double free_start = get_time_us();
    for (size_t i = 0; i < alloc_ok; i++) {
        allocator_free(alloc, ptrs[i]);
    }
    double free_end = get_time_us();

    allocator_stats_t st;
    allocator_get_stats(alloc, &st);
    double util = (st.heap_size == 0) ? 0.0 : ((double)st.peak_requested / (double)st.heap_size);

    double alloc_elapsed = alloc_end - alloc_start;
    double free_elapsed = free_end - free_start;

    benchmark_result_t result = {
        .allocator_name = alloc_name,
        .benchmark_name = "Random",
        .alloc_time_us = alloc_elapsed,
        .free_time_us = free_elapsed,
        .alloc_ops = alloc_ok,
        .free_ops = alloc_ok,
        .alloc_ops_per_sec = (alloc_elapsed <= 0) ? 0.0 : (alloc_ok / (alloc_elapsed / 1000000.0)),
        .free_ops_per_sec = (free_elapsed <= 0) ? 0.0 : (alloc_ok / (free_elapsed / 1000000.0)),
        .peak_utilization = util
    };

    if (output) {
        fprintf(output, "%s,%s,%.2f,%.2f,%zu,%zu,%.2f,%.2f,%.6f\n",
                result.allocator_name, result.benchmark_name,
                result.alloc_time_us, result.free_time_us,
                result.alloc_ops, result.free_ops,
                result.alloc_ops_per_sec, result.free_ops_per_sec,
                result.peak_utilization);
    } else {
        print_result_csv(&result);
    }

    free(ptrs);
}

/* Бенчмарк: сочетание длинных и коротких операций */
void benchmark_mixed(allocator_t* alloc, const char* alloc_name, size_t num_ops, FILE* output) {
    (void)num_ops;
    void* ptrs[500];
    memset(ptrs, 0, sizeof(ptrs));

    allocator_reset_stats(alloc);

    double alloc_time = 0.0;
    double free_time = 0.0;
    size_t alloc_ops = 0;
    size_t free_ops = 0;

    // Фаза 1: выделить 500 маленьких блоков
    double t0 = get_time_us();
    for (int i = 0; i < 500; i++) {
        ptrs[i] = allocator_alloc(alloc, 32);
        if (ptrs[i]) alloc_ops++;
    }
    double t1 = get_time_us();
    alloc_time += (t1 - t0);

    // Фаза 2: освободить половину
    t0 = get_time_us();
    for (int i = 0; i < 500; i += 2) {
        if (ptrs[i]) {
            allocator_free(alloc, ptrs[i]);
            ptrs[i] = NULL;
            free_ops++;
        }
    }
    t1 = get_time_us();
    free_time += (t1 - t0);

    // Фаза 3: выделить 250 более крупных блоков
    t0 = get_time_us();
    for (int i = 0; i < 500; i += 2) {
        ptrs[i] = allocator_alloc(alloc, 128);
        if (ptrs[i]) alloc_ops++;
    }
    t1 = get_time_us();
    alloc_time += (t1 - t0);

    // Фаза 4: освободить всё
    t0 = get_time_us();
    for (int i = 0; i < 500; i++) {
        if (ptrs[i]) {
            allocator_free(alloc, ptrs[i]);
            ptrs[i] = NULL;
            free_ops++;
        }
    }
    t1 = get_time_us();
    free_time += (t1 - t0);

    allocator_stats_t st;
    allocator_get_stats(alloc, &st);
    double util = (st.heap_size == 0) ? 0.0 : ((double)st.peak_requested / (double)st.heap_size);

    benchmark_result_t result = {
        .allocator_name = alloc_name,
        .benchmark_name = "Mixed",
        .alloc_time_us = alloc_time,
        .free_time_us = free_time,
        .alloc_ops = alloc_ops,
        .free_ops = free_ops,
        .alloc_ops_per_sec = (alloc_time <= 0) ? 0.0 : (alloc_ops / (alloc_time / 1000000.0)),
        .free_ops_per_sec = (free_time <= 0) ? 0.0 : (free_ops / (free_time / 1000000.0)),
        .peak_utilization = util
    };

    if (output) {
        fprintf(output, "%s,%s,%.2f,%.2f,%zu,%zu,%.2f,%.2f,%.6f\n",
                result.allocator_name, result.benchmark_name,
                result.alloc_time_us, result.free_time_us,
                result.alloc_ops, result.free_ops,
                result.alloc_ops_per_sec, result.free_ops_per_sec,
                result.peak_utilization);
    } else {
        print_result_csv(&result);
    }
}

/* Бенчмарк: стресс-тест с множеством аллокаций */
void benchmark_stress(allocator_t* alloc, const char* alloc_name, size_t num_ops, FILE* output) {
    void* ptrs[MAX_ALLOCS];
    int allocated = 0;

    allocator_reset_stats(alloc);

    double alloc_start = get_time_us();
    for (int i = 0; i < MAX_ALLOCS && (size_t)i < num_ops; i++) {
        ptrs[i] = allocator_alloc(alloc, 256);
        if (!ptrs[i]) break;
        allocated++;
    }
    double alloc_end = get_time_us();

    double free_start = get_time_us();
    for (int i = 0; i < allocated; i++) {
        allocator_free(alloc, ptrs[i]);
    }
    double free_end = get_time_us();

    allocator_stats_t st;
    allocator_get_stats(alloc, &st);
    double util = (st.heap_size == 0) ? 0.0 : ((double)st.peak_requested / (double)st.heap_size);

    double alloc_elapsed = alloc_end - alloc_start;
    double free_elapsed = free_end - free_start;

    benchmark_result_t result = {
        .allocator_name = alloc_name,
        .benchmark_name = "Stress",
        .alloc_time_us = alloc_elapsed,
        .free_time_us = free_elapsed,
        .alloc_ops = (size_t)allocated,
        .free_ops = (size_t)allocated,
        .alloc_ops_per_sec = (alloc_elapsed <= 0) ? 0.0 : (allocated / (alloc_elapsed / 1000000.0)),
        .free_ops_per_sec = (free_elapsed <= 0) ? 0.0 : (allocated / (free_elapsed / 1000000.0)),
        .peak_utilization = util
    };

    if (output) {
        fprintf(output, "%s,%s,%.2f,%.2f,%zu,%zu,%.2f,%.2f,%.6f\n",
                result.allocator_name, result.benchmark_name,
                result.alloc_time_us, result.free_time_us,
                result.alloc_ops, result.free_ops,
                result.alloc_ops_per_sec, result.free_ops_per_sec,
                result.peak_utilization);
    } else {
        print_result_csv(&result);
    }
}

// при запуске бенчмарков выделяется память для аллокатора, выполняются бенчмарки, память освобождается
void run_benchmarks(allocator_type_t type, const char* name, size_t num_ops, FILE* output) {
    printf("Running benchmarks for %s...\n", name);

    void* backing = malloc(DEFAULT_HEAP_SIZE + 64); // выделяем память с небольшим запасом
    if (!backing) {
        fprintf(stderr, "Failed to allocate backing memory\n");
        return;
    }

    allocator_t* alloc = allocator_create(type, backing, DEFAULT_HEAP_SIZE);
    if (!alloc) {
        fprintf(stderr, "Failed to create allocator: %s\n", name);
        free(backing);
        return;
    }
    benchmark_sequential(alloc, name, num_ops, output);
    allocator_destroy(alloc);
    free(backing);
    // Повторяем для остальных бенчмарков
    backing = malloc(DEFAULT_HEAP_SIZE + 64);
    if (!backing) return;
    alloc = allocator_create(type, backing, DEFAULT_HEAP_SIZE);
    if (!alloc) { free(backing); return; }
    benchmark_random(alloc, name, num_ops, output);
    allocator_destroy(alloc);
    free(backing);

    backing = malloc(DEFAULT_HEAP_SIZE + 64);
    if (!backing) return;
    alloc = allocator_create(type, backing, DEFAULT_HEAP_SIZE);
    if (!alloc) { free(backing); return; }
    benchmark_mixed(alloc, name, num_ops, output);
    allocator_destroy(alloc);
    free(backing);

    backing = malloc(DEFAULT_HEAP_SIZE + 64);
    if (!backing) return;
    alloc = allocator_create(type, backing, DEFAULT_HEAP_SIZE);
    if (!alloc) { free(backing); return; }
    benchmark_stress(alloc, name, num_ops, output);
    allocator_destroy(alloc);
    free(backing);
}

void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -a, --allocator <type>   Allocator type: segregated, buddy, all (default: all)\n");
    printf("  -n, --num-ops <number>   Number of operations (default: 10000)\n");
    printf("  -o, --output <file>      Output CSV file (default: stdout)\n");
    printf("  -h, --help               Show this help message\n");
}

int main(int argc, char* argv[]) {
    allocator_type_t alloc_type = -1;
    size_t num_ops = 10000;
    const char* output_file = NULL;
    bool run_all = true;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--allocator") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing allocator type\n");
                print_usage(argv[0]);
                return 1;
            }
            const char* type = argv[++i];
            if (strcmp(type, "segregated") == 0) {
                alloc_type = ALLOCATOR_SEGREGATED_FREELIST;
                run_all = false;
            } else if (strcmp(type, "buddy") == 0) {
                alloc_type = ALLOCATOR_BUDDY;
                run_all = false;
            } else if (strcmp(type, "all") == 0) {
                run_all = true;
            } else {
                fprintf(stderr, "Error: Unknown allocator type: %s\n", type);
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--num-ops") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing number of operations\n");
                print_usage(argv[0]);
                return 1;
            }
            num_ops = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing output file\n");
                print_usage(argv[0]);
                return 1;
            }
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    FILE* output = NULL;
    if (output_file) {
        output = fopen(output_file, "w");
        if (!output) {
            fprintf(stderr, "Error: Failed to open output file: %s\n", output_file);
            return 1;
        }
    }
    
    printf("=== Memory Allocator Benchmark ===\n");
    printf("Operations per benchmark: %zu\n\n", num_ops);
    
    if (output) {
        fprintf(output, "Allocator,Benchmark,AllocTime_us,FreeTime_us,AllocOps,FreeOps,AllocOpsPerSec,FreeOpsPerSec,PeakUtilization\n");
    } else {
        print_csv_header();
    }
    
    if (run_all) {
        run_benchmarks(ALLOCATOR_SEGREGATED_FREELIST, 
                      "SegregatedFreeList", num_ops, output);
        run_benchmarks(ALLOCATOR_BUDDY, 
                      "Buddy", num_ops, output);
    } else {
        const char* name = (alloc_type == ALLOCATOR_SEGREGATED_FREELIST) ? 
                          "SegregatedFreeList" : "Buddy";
        run_benchmarks(alloc_type, name, num_ops, output);
    }
    
    if (output) {
        fclose(output);
        printf("\nResults written to: %s\n", output_file);
    }
    
    printf("\nBenchmark complete!\n");
    return 0;
}
