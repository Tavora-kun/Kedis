/*
 * kmem 向后兼容层 - 兼容旧版 memory_pool 接口
 * 
 * 允许旧代码无需修改即可使用kmem
 */

#include "../../include/kmem.h"
#include "../../include/memory_pool.h"
#include <string.h>
#include <stdlib.h>

// 初始化内存池（兼容接口）
// 注意：这个接口现在忽略block_size参数，使用kmem的智能分配
memory_pool_t* mem_pool_init(size_t block_size) {
    // 确保kmem已初始化
    kmem_init();
    
    memory_pool_t *pool = (memory_pool_t *)malloc(sizeof(memory_pool_t));
    if (!pool) return NULL;
    
    // 根据请求的block_size选择合适的尺寸类
    int class_idx = kmem_size_class(block_size);
    if (class_idx < 0) {
        // 如果请求的大小超过标准尺寸类，使用最大的
        class_idx = KMEM_CLASS_2KB;
    }
    
    // 初始化pool结构
    pool->block_size = kmem_class_sizes[class_idx];
    pool->chunk_size = kmem_chunk_sizes[class_idx];
    pool->block_count = 0;
    pool->allocated_count = 0;
    pool->free_list = NULL;
    pthread_mutex_init(&pool->lock, NULL);
    pool->max_blocks = 0;
    pool->chunk = NULL;  // 标记为kmem模式
    
    return pool;
}

// 从内存池分配（兼容接口）
void* mem_pool_alloc(memory_pool_t *pool) {
    if (!pool) return NULL;
    
    // 使用kmem分配对应尺寸
    return kmem_alloc(pool->block_size);
}

// 释放内存回内存池（兼容接口）
void mem_pool_free(memory_pool_t *pool, void *ptr) {
    if (!pool || !ptr) return;
    
    // 使用kmem释放
    kmem_free(ptr);
}

// 销毁内存池（兼容接口）
void mem_pool_destroy(memory_pool_t *pool) {
    if (!pool) return;
    
    // 在kmem架构下，只是释放pool结构本身
    // 实际内存由kmem统一管理
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}
