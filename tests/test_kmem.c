/*
 * kmem 单元测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../include/kmem.h"

#define TEST_PASS  "\033[32mPASS\033[0m"
#define TEST_FAIL  "\033[31mFAIL\033[0m"

static int test_count = 0;
static int pass_count = 0;

#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_count++; \
    if (test_##name()) { \
        printf("%s\n", TEST_PASS); \
        pass_count++; \
    } else { \
        printf("%s\n", TEST_FAIL); \
    } \
} while(0)

/* ---------------- 基础测试 ---------------- */

int test_init_destroy(void) {
    if (kmem_init() != 0) return 0;
    kmem_destroy();
    return 1;
}

int test_basic_alloc_free(void) {
    kmem_init();
    
    void *p = kmem_alloc(100);
    if (!p) return 0;
    
    // 写入测试
    memset(p, 0xAB, 100);
    
    kmem_free(p);
    kmem_destroy();
    return 1;
}

int test_size_classes(void) {
    int errors = 0;
    
    // 测试边界值
    if (kmem_size_class(1) != KMEM_CLASS_64B) errors++;
    if (kmem_size_class(64) != KMEM_CLASS_64B) errors++;
    if (kmem_size_class(65) != KMEM_CLASS_128B) errors++;
    if (kmem_size_class(128) != KMEM_CLASS_128B) errors++;
    if (kmem_size_class(129) != KMEM_CLASS_256B) errors++;
    if (kmem_size_class(256) != KMEM_CLASS_256B) errors++;
    if (kmem_size_class(257) != KMEM_CLASS_512B) errors++;
    if (kmem_size_class(512) != KMEM_CLASS_512B) errors++;
    if (kmem_size_class(513) != KMEM_CLASS_1KB) errors++;
    if (kmem_size_class(1024) != KMEM_CLASS_1KB) errors++;
    if (kmem_size_class(1025) != KMEM_CLASS_2KB) errors++;
    if (kmem_size_class(2048) != KMEM_CLASS_2KB) errors++;
    if (kmem_size_class(2049) != -1) errors++; // 大块
    
    return errors == 0;
}

int test_all_size_classes(void) {
    kmem_init();
    
    void *ptrs[KMEM_SIZE_CLASS_COUNT];
    int errors = 0;
    
    // 分配各种尺寸
    for (int i = 0; i < KMEM_SIZE_CLASS_COUNT; i++) {
        ptrs[i] = kmem_alloc(kmem_class_sizes[i]);
        if (!ptrs[i]) errors++;
    }
    
    // 验证块大小
    for (int i = 0; i < KMEM_SIZE_CLASS_COUNT; i++) {
        if (ptrs[i]) {
            size_t bs = kmem_block_size(ptrs[i]);
            if (bs != kmem_class_sizes[i]) {
                printf("(block_size mismatch: expected %zu, got %zu) ",
                       kmem_class_sizes[i], bs);
                errors++;
            }
        }
    }
    
    // 释放
    for (int i = 0; i < KMEM_SIZE_CLASS_COUNT; i++) {
        if (ptrs[i]) kmem_free(ptrs[i]);
    }
    
    kmem_destroy();
    return errors == 0;
}

int test_realloc(void) {
    kmem_init();
    
    void *p = kmem_alloc(50);
    if (!p) return 0;
    
    strcpy((char *)p, "Hello World");
    
    // 扩展到同一尺寸类
    void *p2 = kmem_realloc(p, 60);
    if (!p2) return 0;
    if (strcmp((char *)p2, "Hello World") != 0) return 0;
    
    // 扩展到不同尺寸类
    void *p3 = kmem_realloc(p2, 500);
    if (!p3) return 0;
    if (strcmp((char *)p3, "Hello World") != 0) return 0;
    
    kmem_free(p3);
    kmem_destroy();
    return 1;
}

int test_large_alloc(void) {
    kmem_init();
    
    void *p = kmem_alloc(3000); // > 2KB
    if (!p) return 0;
    
    memset(p, 0, 3000);
    kmem_free(p);
    
    kmem_destroy();
    return 1;
}

/* ---------------- 压力测试 ---------------- */

int test_stress_single_thread(void) {
    kmem_init();
    
    const int iter = 100000;
    void *ptrs[100] = {NULL};
    int errors = 0;
    
    for (int i = 0; i < iter; i++) {
        // 随机分配
        int idx = rand() % 100;
        if (ptrs[idx]) {
            kmem_free(ptrs[idx]);
            ptrs[idx] = NULL;
        }
        
        size_t size = (rand() % 2000) + 1;
        ptrs[idx] = kmem_alloc(size);
        if (!ptrs[idx]) {
            errors++;
        } else {
            // 写入模式验证
            memset(ptrs[idx], idx & 0xFF, size > 50 ? 50 : size);
        }
    }
    
    // 清理
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) kmem_free(ptrs[i]);
    }
    
    kmem_destroy();
    return errors == 0;
}

int test_chunk_growth(void) {
    kmem_init();
    
    // 分配大量块触发chunk扩展
    const int count = 100000;
    void **ptrs = calloc(count, sizeof(void *));
    int errors = 0;
    
    for (int i = 0; i < count; i++) {
        ptrs[i] = kmem_alloc(64); // 64B类
        if (!ptrs[i]) {
            errors++;
            break;
        }
    }
    
    for (int i = 0; i < count && ptrs[i]; i++) {
        kmem_free(ptrs[i]);
    }
    
    free(ptrs);
    kmem_destroy();
    return errors == 0;
}

/* ---------------- TLS测试 ---------------- */

int test_tls_basic(void) {
    kmem_init();
    kmem_tls_init();
    
    void *p = kmem_alloc_fast(100);
    if (!p) return 0;
    
    memset(p, 0xAB, 100);
    kmem_free_fast(p);
    
    kmem_tls_destroy();
    kmem_destroy();
    return 1;
}

int test_tls_stress(void) {
    kmem_init();
    kmem_tls_init();
    
    const int iter = 100000;
    void *ptrs[100] = {NULL};
    int errors = 0;
    
    for (int i = 0; i < iter; i++) {
        int idx = rand() % 100;
        if (ptrs[idx]) {
            kmem_free_fast(ptrs[idx]);
            ptrs[idx] = NULL;
        }
        
        size_t size = (rand() % 2000) + 1;
        ptrs[idx] = kmem_alloc_fast(size);
        if (!ptrs[idx]) errors++;
    }
    
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) kmem_free_fast(ptrs[i]);
    }
    
    kmem_tls_destroy();
    kmem_destroy();
    return errors == 0;
}

/* ---------------- 多线程测试 ---------------- */

typedef struct {
    int thread_id;
    int iter_count;
    int errors;
} thread_args_t;

void *thread_worker(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    kmem_tls_init();
    
    void *ptrs[50] = {NULL};
    
    for (int i = 0; i < args->iter_count; i++) {
        int idx = rand() % 50;
        if (ptrs[idx]) {
            kmem_free_fast(ptrs[idx]);
            ptrs[idx] = NULL;
        }
        
        size_t size = (rand() % 2000) + 1;
        ptrs[idx] = kmem_alloc_fast(size);
        if (!ptrs[idx]) {
            args->errors++;
        } else {
            memset(ptrs[idx], args->thread_id, 16);
        }
    }
    
    for (int i = 0; i < 50; i++) {
        if (ptrs[i]) kmem_free_fast(ptrs[i]);
    }
    
    kmem_tls_destroy();
    return NULL;
}

int test_multi_thread(void) {
    kmem_init();
    
    const int num_threads = 8;
    const int iter_per_thread = 10000;
    
    pthread_t threads[num_threads];
    thread_args_t args[num_threads];
    
    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        args[i].iter_count = iter_per_thread;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, thread_worker, &args[i]);
    }
    
    int total_errors = 0;
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        total_errors += args[i].errors;
    }
    
    kmem_destroy();
    return total_errors == 0;
}

/* ---------------- 性能测试 ---------------- */

double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int test_performance(void) {
    kmem_init();
    kmem_tls_init();
    
    const int iter = 1000000;
    double t1, t2;
    
    // 测试标准malloc/free
    t1 = get_time();
    for (int i = 0; i < iter; i++) {
        void *p = malloc(128);
        free(p);
    }
    t2 = get_time();
    double malloc_time = t2 - t1;
    printf("\n  malloc/free: %.3f sec (%.0f ops/sec) ", 
           malloc_time, iter / malloc_time);
    
    // 测试kmem_alloc/free
    t1 = get_time();
    for (int i = 0; i < iter; i++) {
        void *p = kmem_alloc(128);
        kmem_free(p);
    }
    t2 = get_time();
    double kmem_time = t2 - t1;
    printf("\n  kmem_alloc/free: %.3f sec (%.0f ops/sec, %.1fx) ",
           kmem_time, iter / kmem_time, malloc_time / kmem_time);
    
    // 测试kmem_alloc_fast/free_fast (TLS)
    t1 = get_time();
    for (int i = 0; i < iter; i++) {
        void *p = kmem_alloc_fast(128);
        kmem_free_fast(p);
    }
    t2 = get_time();
    double fast_time = t2 - t1;
    printf("\n  kmem_alloc_fast/free_fast: %.3f sec (%.0f ops/sec, %.1fx) ",
           fast_time, iter / fast_time, malloc_time / fast_time);
    
    kmem_tls_destroy();
    kmem_destroy();
    return 1;
}

/* ---------------- 主函数 ---------------- */

int main(void) {
    printf("\n========================================\n");
    printf("       kmem Unit Tests\n");
    printf("========================================\n\n");
    
    // 基础测试
    printf("--- Basic Tests ---\n");
    RUN_TEST(init_destroy);
    RUN_TEST(basic_alloc_free);
    RUN_TEST(size_classes);
    RUN_TEST(all_size_classes);
    RUN_TEST(realloc);
    RUN_TEST(large_alloc);
    
    // 压力测试
    printf("\n--- Stress Tests ---\n");
    RUN_TEST(stress_single_thread);
    RUN_TEST(chunk_growth);
    
    // TLS测试
    printf("\n--- TLS Tests ---\n");
    RUN_TEST(tls_basic);
    RUN_TEST(tls_stress);
    
    // 多线程测试
    printf("\n--- Multi-thread Tests ---\n");
    RUN_TEST(multi_thread);
    
    // 性能测试
    printf("\n--- Performance Tests ---\n");
    RUN_TEST(performance);
    
    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", pass_count, test_count);
    printf("========================================\n\n");
    
    // 打印内存统计
    kmem_init();
    printf("Memory pool after init:\n");
    kmem_stats_print();
    
    // 分配一些内存后
    void *p1 = kmem_alloc(100);
    void *p2 = kmem_alloc(500);
    void *p3 = kmem_alloc(1500);
    
    printf("After allocating 100B, 500B, 1500B:\n");
    kmem_stats_print();
    
    kmem_free(p1);
    kmem_free(p2);
    kmem_free(p3);
    kmem_destroy();
    
    return (pass_count == test_count) ? 0 : 1;
}
