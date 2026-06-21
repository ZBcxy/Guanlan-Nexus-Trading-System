/*
 * GNTS - 观澜 · Nexus Trading System
 * 内存池实现
 */

#include "common/memory_pool.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* 全局内存池 */
memory_pool_t* g_small_pool = NULL;
memory_pool_t* g_medium_pool = NULL;
memory_pool_t* g_large_pool = NULL;
memory_pool_t* g_default_pool = NULL;

/* 块大小定义 */
#define BLOCK_SIZE_SMALL 64
#define BLOCK_SIZE_MEDIUM 256
#define BLOCK_SIZE_LARGE 1024
#define BLOCK_SIZE_HUGE 4096

/* 创建内存块 */
static mem_block_t* create_block(size_t size, mem_block_type_t type) {
    mem_block_t* block = (mem_block_t*)malloc(sizeof(mem_block_t) + size);
    if (block == NULL) {
        return NULL;
    }

    block->size = size;
    block->in_use = 0;
    block->next = NULL;
    block->block_type = type;

    return block;
}

/* 创建内存池 */
memory_pool_t* memory_pool_create(int small_count, int medium_count,
                                   int large_count, int huge_count) {
    memory_pool_t* pool = (memory_pool_t*)malloc(sizeof(memory_pool_t));
    if (pool == NULL) {
        LOG_ERROR("Failed to allocate memory pool");
        return NULL;
    }

    pool->small_blocks = NULL;
    pool->medium_blocks = NULL;
    pool->large_blocks = NULL;
    pool->huge_blocks = NULL;

    pool->small_count = small_count;
    pool->medium_count = medium_count;
    pool->large_count = large_count;
    pool->huge_count = huge_count;

    pool->small_free = small_count;
    pool->medium_free = medium_count;
    pool->large_free = large_count;
    pool->huge_free = huge_count;

    pthread_mutex_init(&pool->mutex, NULL);

    pool->total_size = 0;
    pool->used_size = 0;

    /* 创建小块 */
    for (int i = 0; i < small_count; i++) {
        mem_block_t* block = create_block(BLOCK_SIZE_SMALL, MEM_BLOCK_SMALL);
        if (block == NULL) {
            memory_pool_destroy(pool);
            return NULL;
        }

        block->next = pool->small_blocks;
        pool->small_blocks = block;
        pool->total_size += BLOCK_SIZE_SMALL;
    }

    /* 创建中块 */
    for (int i = 0; i < medium_count; i++) {
        mem_block_t* block = create_block(BLOCK_SIZE_MEDIUM, MEM_BLOCK_MEDIUM);
        if (block == NULL) {
            memory_pool_destroy(pool);
            return NULL;
        }

        block->next = pool->medium_blocks;
        pool->medium_blocks = block;
        pool->total_size += BLOCK_SIZE_MEDIUM;
    }

    /* 创建大块 */
    for (int i = 0; i < large_count; i++) {
        mem_block_t* block = create_block(BLOCK_SIZE_LARGE, MEM_BLOCK_LARGE);
        if (block == NULL) {
            memory_pool_destroy(pool);
            return NULL;
        }

        block->next = pool->large_blocks;
        pool->large_blocks = block;
        pool->total_size += BLOCK_SIZE_LARGE;
    }

    /* 创建巨块 */
    for (int i = 0; i < huge_count; i++) {
        mem_block_t* block = create_block(BLOCK_SIZE_HUGE, MEM_BLOCK_HUGE);
        if (block == NULL) {
            memory_pool_destroy(pool);
            return NULL;
        }

        block->next = pool->huge_blocks;
        pool->huge_blocks = block;
        pool->total_size += BLOCK_SIZE_HUGE;
    }

    LOG_INFO("Memory pool created: total_size=%zu bytes", pool->total_size);
    return pool;
}

/* 销毁内存池 */
void memory_pool_destroy(memory_pool_t* pool) {
    if (pool == NULL) {
        return;
    }

    /* 释放所有块 */
    mem_block_t* block = pool->small_blocks;
    while (block != NULL) {
        mem_block_t* next = block->next;
        free(block);
        block = next;
    }

    block = pool->medium_blocks;
    while (block != NULL) {
        mem_block_t* next = block->next;
        free(block);
        block = next;
    }

    block = pool->large_blocks;
    while (block != NULL) {
        mem_block_t* next = block->next;
        free(block);
        block = next;
    }

    block = pool->huge_blocks;
    while (block != NULL) {
        mem_block_t* next = block->next;
        free(block);
        block = next;
    }

    pthread_mutex_destroy(&pool->mutex);
    free(pool);

    LOG_INFO("Memory pool destroyed");
}

/* 从指定链表分配块 */
static mem_block_t* alloc_from_list(mem_block_t** list, int* free_count) {
    mem_block_t* block = *list;

    while (block != NULL) {
        if (!block->in_use) {
            block->in_use = 1;
            (*free_count)--;
            return block;
        }
        block = block->next;
    }

    return NULL;
}

/* 从内存池分配内存 */
void* memory_pool_alloc(memory_pool_t* pool, size_t size) {
    if (pool == NULL || size == 0) {
        return NULL;
    }

    pthread_mutex_lock(&pool->mutex);

    mem_block_t* block = NULL;

    /* 根据大小选择合适的块 */
    if (size <= BLOCK_SIZE_SMALL && pool->small_free > 0) {
        block = alloc_from_list(&pool->small_blocks, &pool->small_free);
    } else if (size <= BLOCK_SIZE_MEDIUM && pool->medium_free > 0) {
        block = alloc_from_list(&pool->medium_blocks, &pool->medium_free);
    } else if (size <= BLOCK_SIZE_LARGE && pool->large_free > 0) {
        block = alloc_from_list(&pool->large_blocks, &pool->large_free);
    } else if (size <= BLOCK_SIZE_HUGE && pool->huge_free > 0) {
        block = alloc_from_list(&pool->huge_blocks, &pool->huge_free);
    }

    if (block != NULL) {
        pool->used_size += block->size;
        pthread_mutex_unlock(&pool->mutex);
        LOG_DEBUG("Allocated %zu bytes from pool", block->size);
        return block->data;
    }

    /* 没有合适的块，直接分配 */
    pthread_mutex_unlock(&pool->mutex);
    void* ptr = malloc(size);
    if (ptr != NULL) {
        LOG_DEBUG("Allocated %zu bytes from system", size);
    }
    return ptr;
}

/* 获取块类型名称 */
static const char* block_type_name(mem_block_type_t type) {
    switch (type) {
        case MEM_BLOCK_SMALL:  return "small";
        case MEM_BLOCK_MEDIUM: return "medium";
        case MEM_BLOCK_LARGE:  return "large";
        case MEM_BLOCK_HUGE:   return "huge";
        default:               return "unknown";
    }
}

/* 释放内存到内存池（使用 block_type 避免 O(n) 全表扫描） */
void memory_pool_free(memory_pool_t* pool, void* ptr) {
    if (pool == NULL || ptr == NULL) {
        return;
    }

    /* 从数据指针恢复块指针 */
    mem_block_t* block = (mem_block_t*)((char*)ptr - offsetof(mem_block_t, data));

    pthread_mutex_lock(&pool->mutex);

    /* 根据块类型确定要搜索的链表 */
    mem_block_t** list_head = NULL;
    int* free_count = NULL;

    switch (block->block_type) {
        case MEM_BLOCK_SMALL:
            list_head = &pool->small_blocks;
            free_count = &pool->small_free;
            break;
        case MEM_BLOCK_MEDIUM:
            list_head = &pool->medium_blocks;
            free_count = &pool->medium_free;
            break;
        case MEM_BLOCK_LARGE:
            list_head = &pool->large_blocks;
            free_count = &pool->large_free;
            break;
        case MEM_BLOCK_HUGE:
            list_head = &pool->huge_blocks;
            free_count = &pool->huge_free;
            break;
        default:
            /* 不是池中的内存，直接释放 */
            pthread_mutex_unlock(&pool->mutex);
            free(ptr);
            LOG_DEBUG("Freed system memory (unknown block type)");
            return;
    }

    /* 在单个链表中查找并释放 */
    mem_block_t* current = *list_head;
    while (current != NULL) {
        if (current == block) {
            current->in_use = 0;
            (*free_count)++;
            pool->used_size -= current->size;
            pthread_mutex_unlock(&pool->mutex);
            LOG_DEBUG("Freed %s block (%zu bytes)", block_type_name(block->block_type), block->size);
            return;
        }
        current = current->next;
    }

    /* 块不在池的链表中（属于其他池或被错误释放） */
    pthread_mutex_unlock(&pool->mutex);
    LOG_WARNING("Block not found in pool's %s list", block_type_name(block->block_type));
}

/* 获取内存池统计信息 */
void memory_pool_stats(memory_pool_t* pool, size_t* total, size_t* used, size_t* free) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    if (total != NULL) {
        *total = pool->total_size;
    }

    if (used != NULL) {
        *used = pool->used_size;
    }

    if (free != NULL) {
        *free = pool->total_size - pool->used_size;
    }

    pthread_mutex_unlock(&pool->mutex);
}

/* 重置内存池 */
void memory_pool_reset(memory_pool_t* pool) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    /* 重置所有块 */
    mem_block_t* block = pool->small_blocks;
    while (block != NULL) {
        block->in_use = 0;
        block = block->next;
    }

    block = pool->medium_blocks;
    while (block != NULL) {
        block->in_use = 0;
        block = block->next;
    }

    block = pool->large_blocks;
    while (block != NULL) {
        block->in_use = 0;
        block = block->next;
    }

    block = pool->huge_blocks;
    while (block != NULL) {
        block->in_use = 0;
        block = block->next;
    }

    pool->small_free = pool->small_count;
    pool->medium_free = pool->medium_count;
    pool->large_free = pool->large_count;
    pool->huge_free = pool->huge_count;
    pool->used_size = 0;

    pthread_mutex_unlock(&pool->mutex);
    LOG_INFO("Memory pool reset");
}

/* 初始化全局内存池 */
int init_global_memory_pools(void) {
    g_small_pool = memory_pool_create(1000, 0, 0, 0);
    g_medium_pool = memory_pool_create(0, 500, 0, 0);
    g_large_pool = memory_pool_create(0, 0, 200, 0);
    g_default_pool = memory_pool_create(100, 100, 100, 50);

    if (g_small_pool == NULL || g_medium_pool == NULL ||
        g_large_pool == NULL || g_default_pool == NULL) {
        destroy_global_memory_pools();
        return GNTS_ERROR_MEMORY;
    }

    LOG_INFO("Global memory pools initialized");
    return GNTS_OK;
}

/* 从全局内存池快速释放（根据 block_type 自动定位正确的池） */
void fast_free_global(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    /* 从数据指针恢复块指针 */
    mem_block_t* block = (mem_block_t*)((char*)ptr - offsetof(mem_block_t, data));

    /* 根据块类型选择正确的全局池 */
    switch (block->block_type) {
        case MEM_BLOCK_SMALL:
            memory_pool_free(g_small_pool, ptr);
            return;
        case MEM_BLOCK_MEDIUM:
            memory_pool_free(g_medium_pool, ptr);
            return;
        case MEM_BLOCK_LARGE:
            memory_pool_free(g_large_pool, ptr);
            return;
        case MEM_BLOCK_HUGE:
            /* 巨块可能在 g_default_pool 或 g_large_pool 中，先试 default */
            memory_pool_free(g_default_pool, ptr);
            return;
        default:
            /* 不是池分配的内存 */
            free(ptr);
            return;
    }
}

/* 销毁全局内存池 */
void destroy_global_memory_pools(void) {
    if (g_small_pool != NULL) {
        memory_pool_destroy(g_small_pool);
        g_small_pool = NULL;
    }

    if (g_medium_pool != NULL) {
        memory_pool_destroy(g_medium_pool);
        g_medium_pool = NULL;
    }

    if (g_large_pool != NULL) {
        memory_pool_destroy(g_large_pool);
        g_large_pool = NULL;
    }

    if (g_default_pool != NULL) {
        memory_pool_destroy(g_default_pool);
        g_default_pool = NULL;
    }

    LOG_INFO("Global memory pools destroyed");
}