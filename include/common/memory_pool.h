/*
 * GNTS - 观澜 · Nexus Trading System
 * 内存池 - 高性能内存管理
 */

#ifndef GNTS_COMMON_MEMORY_POOL_H
#define GNTS_COMMON_MEMORY_POOL_H

#include "common/types.h"
#include <pthread.h>

/* 内存块大小类型 */
typedef enum {
    MEM_BLOCK_SMALL = 1,    /* 小块：64字节 */
    MEM_BLOCK_MEDIUM = 2,   /* 中块：256字节 */
    MEM_BLOCK_LARGE = 3,    /* 大块：1024字节 */
    MEM_BLOCK_HUGE = 4,     /* 巨块：4096字节 */
} mem_block_type_t;

/* 内存块 */
typedef struct mem_block {
    struct mem_block* next;
    size_t size;
    int in_use;
    mem_block_type_t block_type;  /* 块大小类型（快速定位链表，避免 O(n) 扫描） */
    char data[];                  /* 柔性数组 */
} mem_block_t;

/* 内存池 */
typedef struct {
    mem_block_t* small_blocks;   /* 小块链表 */
    mem_block_t* medium_blocks;  /* 中块链表 */
    mem_block_t* large_blocks;   /* 大块链表 */
    mem_block_t* huge_blocks;    /* 巨块链表 */

    int small_count;             /* 小块数量 */
    int medium_count;            /* 中块数量 */
    int large_count;             /* 大块数量 */
    int huge_count;              /* 巨块数量 */

    int small_free;              /* 小块空闲数量 */
    int medium_free;             /* 中块空闲数量 */
    int large_free;              /* 大块空闲数量 */
    int huge_free;               /* 巨块空闲数量 */

    pthread_mutex_t mutex;       /* 互斥锁 */
    size_t total_size;           /* 总内存大小 */
    size_t used_size;            /* 已使用内存大小 */
} memory_pool_t;

/* 创建内存池 */
memory_pool_t* memory_pool_create(int small_count, int medium_count,
                                   int large_count, int huge_count);

/* 销毁内存池 */
void memory_pool_destroy(memory_pool_t* pool);

/* 从内存池分配内存 */
void* memory_pool_alloc(memory_pool_t* pool, size_t size);

/* 释放内存到内存池 */
void memory_pool_free(memory_pool_t* pool, void* ptr);

/* 获取内存池统计信息 */
void memory_pool_stats(memory_pool_t* pool, size_t* total, size_t* used, size_t* free);

/* 重置内存池（释放所有内存） */
void memory_pool_reset(memory_pool_t* pool);

/* ============ 快速内存分配宏 ============ */

/* 预定义内存池 */
extern memory_pool_t* g_small_pool;
extern memory_pool_t* g_medium_pool;
extern memory_pool_t* g_large_pool;

/* 快速分配宏 */
#define FAST_ALLOC_SMALL() memory_pool_alloc(g_small_pool, 64)
#define FAST_ALLOC_MEDIUM() memory_pool_alloc(g_medium_pool, 256)
#define FAST_ALLOC_LARGE() memory_pool_alloc(g_large_pool, 1024)

/* 从全局内存池释放（自动定位正确的池） */
void fast_free_global(void* ptr);

#define FAST_FREE(ptr) fast_free_global(ptr)

/* 初始化全局内存池 */
int init_global_memory_pools(void);

/* 销毁全局内存池 */
void destroy_global_memory_pools(void);

#endif /* GNTS_COMMON_MEMORY_POOL_H */