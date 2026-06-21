/*
 * GNTS - 观澜 · Nexus Trading System
 * 内存存储实现
 */

#include "data_system/data_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* K线索引键 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    bar_period_t period;
    timestamp_t timestamp;
} bar_key_t;

/* Tick索引键 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    timestamp_t timestamp;
} tick_key_t;

/* K线数据节点 */
typedef struct bar_node {
    bar_data_t data;
    struct bar_node* next;
} bar_node_t;

/* Tick数据节点 */
typedef struct tick_node {
    tick_data_t data;
    struct tick_node* next;
} tick_node_t;

/* K线哈希表 */
typedef struct {
    bar_node_t** buckets;
    int bucket_count;
    int count;
} bar_hash_table_t;

/* Tick哈希表 */
typedef struct {
    tick_node_t** buckets;
    int bucket_count;
    int count;
} tick_hash_table_t;

/* 内存存储上下文 */
typedef struct {
    bar_hash_table_t* bar_table;
    tick_hash_table_t* tick_table;
    pthread_mutex_t mutex;
    int initialized;
} memory_storage_t;

/* 全局内存存储实例 */
static memory_storage_t g_memory_storage = {0};

/* 哈希函数 */
static unsigned int hash_string(const char* str, int bucket_count) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % bucket_count;
}

/* 创建K线哈希表 */
static bar_hash_table_t* create_bar_table(int bucket_count) {
    bar_hash_table_t* table = (bar_hash_table_t*)malloc(sizeof(bar_hash_table_t));
    if (table == NULL) {
        return NULL;
    }

    table->buckets = (bar_node_t**)calloc(bucket_count, sizeof(bar_node_t*));
    if (table->buckets == NULL) {
        free(table);
        return NULL;
    }

    table->bucket_count = bucket_count;
    table->count = 0;
    return table;
}

/* 创建Tick哈希表 */
static tick_hash_table_t* create_tick_table(int bucket_count) {
    tick_hash_table_t* table = (tick_hash_table_t*)malloc(sizeof(tick_hash_table_t));
    if (table == NULL) {
        return NULL;
    }

    table->buckets = (tick_node_t**)calloc(bucket_count, sizeof(tick_node_t*));
    if (table->buckets == NULL) {
        free(table);
        return NULL;
    }

    table->bucket_count = bucket_count;
    table->count = 0;
    return table;
}

/* 初始化内存存储 */
static int memory_storage_init(const storage_config_t* config) {
    if (g_memory_storage.initialized) {
        return GNTS_OK;
    }

    g_memory_storage.bar_table = create_bar_table(10007);
    if (g_memory_storage.bar_table == NULL) {
        LOG_ERROR("Failed to create bar table");
        return GNTS_ERROR_MEMORY;
    }

    g_memory_storage.tick_table = create_tick_table(10007);
    if (g_memory_storage.tick_table == NULL) {
        LOG_ERROR("Failed to create tick table");
        free(g_memory_storage.bar_table->buckets);
        free(g_memory_storage.bar_table);
        return GNTS_ERROR_MEMORY;
    }

    pthread_mutex_init(&g_memory_storage.mutex, NULL);
    g_memory_storage.initialized = 1;

    LOG_INFO("Memory storage initialized");
    return GNTS_OK;
}

/* 关闭内存存储 */
static void memory_storage_shutdown(void) {
    if (!g_memory_storage.initialized) {
        return;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    /* 释放K线数据 */
    if (g_memory_storage.bar_table != NULL) {
        for (int i = 0; i < g_memory_storage.bar_table->bucket_count; i++) {
            bar_node_t* node = g_memory_storage.bar_table->buckets[i];
            while (node != NULL) {
                bar_node_t* next = node->next;
                free(node);
                node = next;
            }
        }
        free(g_memory_storage.bar_table->buckets);
        free(g_memory_storage.bar_table);
    }

    /* 释放Tick数据 */
    if (g_memory_storage.tick_table != NULL) {
        for (int i = 0; i < g_memory_storage.tick_table->bucket_count; i++) {
            tick_node_t* node = g_memory_storage.tick_table->buckets[i];
            while (node != NULL) {
                tick_node_t* next = node->next;
                free(node);
                node = next;
            }
        }
        free(g_memory_storage.tick_table->buckets);
        free(g_memory_storage.tick_table);
    }

    g_memory_storage.initialized = 0;

    pthread_mutex_unlock(&g_memory_storage.mutex);
    pthread_mutex_destroy(&g_memory_storage.mutex);

    LOG_INFO("Memory storage shutdown");
}

/* 保存单根K线 */
static int memory_storage_save_bar(const bar_data_t* bar) {
    if (bar == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    unsigned int hash = hash_string(bar->symbol, g_memory_storage.bar_table->bucket_count);
    bar_node_t* node = g_memory_storage.bar_table->buckets[hash];

    /* 查找是否已存在 */
    while (node != NULL) {
        if (strcmp(node->data.symbol, bar->symbol) == 0 &&
            node->data.period == bar->period &&
            node->data.timestamp == bar->timestamp) {
            /* 更新 */
            node->data = *bar;
            pthread_mutex_unlock(&g_memory_storage.mutex);
            return GNTS_OK;
        }
        node = node->next;
    }

    /* 创建新节点 */
    bar_node_t* new_node = (bar_node_t*)malloc(sizeof(bar_node_t));
    if (new_node == NULL) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        LOG_ERROR("Failed to allocate memory for bar node");
        return GNTS_ERROR_MEMORY;
    }

    new_node->data = *bar;
    new_node->next = g_memory_storage.bar_table->buckets[hash];
    g_memory_storage.bar_table->buckets[hash] = new_node;
    g_memory_storage.bar_table->count++;

    pthread_mutex_unlock(&g_memory_storage.mutex);
    return GNTS_OK;
}

/* 批量保存K线 */
static int memory_storage_save_bars(const bar_data_t* bars, int count) {
    if (bars == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < count; i++) {
        int ret = memory_storage_save_bar(&bars[i]);
        if (ret != GNTS_OK) {
            return ret;
        }
    }

    LOG_INFO("Saved %d bars to memory storage", count);
    return GNTS_OK;
}

/* 保存单个Tick */
static int memory_storage_save_tick(const tick_data_t* tick) {
    if (tick == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    unsigned int hash = hash_string(tick->symbol, g_memory_storage.tick_table->bucket_count);
    tick_node_t* node = g_memory_storage.tick_table->buckets[hash];

    /* 查找是否已存在 */
    while (node != NULL) {
        if (strcmp(node->data.symbol, tick->symbol) == 0 &&
            node->data.timestamp == tick->timestamp) {
            /* 更新 */
            node->data = *tick;
            pthread_mutex_unlock(&g_memory_storage.mutex);
            return GNTS_OK;
        }
        node = node->next;
    }

    /* 创建新节点 */
    tick_node_t* new_node = (tick_node_t*)malloc(sizeof(tick_node_t));
    if (new_node == NULL) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        LOG_ERROR("Failed to allocate memory for tick node");
        return GNTS_ERROR_MEMORY;
    }

    new_node->data = *tick;
    new_node->next = g_memory_storage.tick_table->buckets[hash];
    g_memory_storage.tick_table->buckets[hash] = new_node;
    g_memory_storage.tick_table->count++;

    pthread_mutex_unlock(&g_memory_storage.mutex);
    return GNTS_OK;
}

/* 批量保存Tick */
static int memory_storage_save_ticks(const tick_data_t* ticks, int count) {
    if (ticks == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < count; i++) {
        int ret = memory_storage_save_tick(&ticks[i]);
        if (ret != GNTS_OK) {
            return ret;
        }
    }

    LOG_INFO("Saved %d ticks to memory storage", count);
    return GNTS_OK;
}

/* 查询K线 */
static int memory_storage_query_bars(const char* symbol, timestamp_t start, timestamp_t end,
                                     bar_period_t period, bar_data_t** bars, int* count) {
    if (symbol == NULL || bars == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    /* 先统计数量 */
    int total = 0;
    unsigned int hash = hash_string(symbol, g_memory_storage.bar_table->bucket_count);
    bar_node_t* node = g_memory_storage.bar_table->buckets[hash];

    while (node != NULL) {
        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.period == period &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            total++;
        }
        node = node->next;
    }

    if (total == 0) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        *count = 0;
        *bars = NULL;
        return GNTS_OK;
    }

    /* 分配内存 */
    *bars = (bar_data_t*)malloc(sizeof(bar_data_t) * total);
    if (*bars == NULL) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        LOG_ERROR("Failed to allocate memory for bars");
        return GNTS_ERROR_MEMORY;
    }

    /* 复制数据 */
    int index = 0;
    node = g_memory_storage.bar_table->buckets[hash];
    while (node != NULL) {
        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.period == period &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            (*bars)[index++] = node->data;
        }
        node = node->next;
    }

    *count = total;

    pthread_mutex_unlock(&g_memory_storage.mutex);
    LOG_INFO("Queried %d bars for %s", total, symbol);
    return GNTS_OK;
}

/* 查询Tick */
static int memory_storage_query_ticks(const char* symbol, timestamp_t start, timestamp_t end,
                                      tick_data_t** ticks, int* count) {
    if (symbol == NULL || ticks == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    /* 先统计数量 */
    int total = 0;
    unsigned int hash = hash_string(symbol, g_memory_storage.tick_table->bucket_count);
    tick_node_t* node = g_memory_storage.tick_table->buckets[hash];

    while (node != NULL) {
        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            total++;
        }
        node = node->next;
    }

    if (total == 0) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        *count = 0;
        *ticks = NULL;
        return GNTS_OK;
    }

    /* 分配内存 */
    *ticks = (tick_data_t*)malloc(sizeof(tick_data_t) * total);
    if (*ticks == NULL) {
        pthread_mutex_unlock(&g_memory_storage.mutex);
        LOG_ERROR("Failed to allocate memory for ticks");
        return GNTS_ERROR_MEMORY;
    }

    /* 复制数据 */
    int index = 0;
    node = g_memory_storage.tick_table->buckets[hash];
    while (node != NULL) {
        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            (*ticks)[index++] = node->data;
        }
        node = node->next;
    }

    *count = total;

    pthread_mutex_unlock(&g_memory_storage.mutex);
    LOG_INFO("Queried %d ticks for %s", total, symbol);
    return GNTS_OK;
}

/* 删除K线 */
static int memory_storage_delete_bars(const char* symbol, timestamp_t start, timestamp_t end) {
    if (symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    unsigned int hash = hash_string(symbol, g_memory_storage.bar_table->bucket_count);
    bar_node_t* prev = NULL;
    bar_node_t* node = g_memory_storage.bar_table->buckets[hash];
    int deleted = 0;

    while (node != NULL) {
        bar_node_t* next = node->next;

        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            /* 删除节点 */
            if (prev == NULL) {
                g_memory_storage.bar_table->buckets[hash] = next;
            } else {
                prev->next = next;
            }
            free(node);
            deleted++;
            g_memory_storage.bar_table->count--;
        } else {
            prev = node;
        }

        node = next;
    }

    pthread_mutex_unlock(&g_memory_storage.mutex);
    LOG_INFO("Deleted %d bars for %s", deleted, symbol);
    return GNTS_OK;
}

/* 删除Tick */
static int memory_storage_delete_ticks(const char* symbol, timestamp_t start, timestamp_t end) {
    if (symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (!g_memory_storage.initialized) {
        LOG_ERROR("Memory storage not initialized");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&g_memory_storage.mutex);

    unsigned int hash = hash_string(symbol, g_memory_storage.tick_table->bucket_count);
    tick_node_t* prev = NULL;
    tick_node_t* node = g_memory_storage.tick_table->buckets[hash];
    int deleted = 0;

    while (node != NULL) {
        tick_node_t* next = node->next;

        if (strcmp(node->data.symbol, symbol) == 0 &&
            node->data.timestamp >= start &&
            node->data.timestamp <= end) {
            /* 删除节点 */
            if (prev == NULL) {
                g_memory_storage.tick_table->buckets[hash] = next;
            } else {
                prev->next = next;
            }
            free(node);
            deleted++;
            g_memory_storage.tick_table->count--;
        } else {
            prev = node;
        }

        node = next;
    }

    pthread_mutex_unlock(&g_memory_storage.mutex);
    LOG_INFO("Deleted %d ticks for %s", deleted, symbol);
    return GNTS_OK;
}

/* 内存存储接口 */
storage_interface_t g_memory_storage_interface = {
    .init = memory_storage_init,
    .shutdown = memory_storage_shutdown,
    .save_bar = memory_storage_save_bar,
    .save_bars = memory_storage_save_bars,
    .save_tick = memory_storage_save_tick,
    .save_ticks = memory_storage_save_ticks,
    .query_bars = memory_storage_query_bars,
    .query_ticks = memory_storage_query_ticks,
    .delete_bars = memory_storage_delete_bars,
    .delete_ticks = memory_storage_delete_ticks
};

/* 获取内存存储接口 */
storage_interface_t* get_memory_storage(void) {
    return &g_memory_storage_interface;
}