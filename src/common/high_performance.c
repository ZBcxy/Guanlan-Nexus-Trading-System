/*
 * GNTS - 观澜 · Nexus Trading System
 * 高性能数据结构实现
 */

#include "common/high_performance.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

/* ============ 哈希函数 ============ */

/* MurmurHash3 */
static uint32_t murmurhash3(const char* key) {
    uint32_t hash = 0;

    if (key == NULL) {
        return 0;
    }

    const uint8_t* data = (const uint8_t*)key;
    int len = strlen(key);

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const int nblocks = len / 4;

    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);

    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];

        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;

        hash ^= k1;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }

    const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);

    uint32_t k1 = 0;

    switch (len & 3) {
        case 3:
            k1 ^= tail[2] << 16;
            k1 ^= tail[1] << 8;
            k1 ^= tail[0];
            k1 *= c1;
            k1 = (k1 << 15) | (k1 >> 17);
            k1 *= c2;
            hash ^= k1;
            break;
        case 2:
            k1 ^= tail[1] << 8;
            k1 ^= tail[0];
            k1 *= c1;
            k1 = (k1 << 15) | (k1 >> 17);
            k1 *= c2;
            hash ^= k1;
            break;
        case 1:
            k1 ^= tail[0];
            k1 *= c1;
            k1 = (k1 << 15) | (k1 >> 17);
            k1 *= c2;
            hash ^= k1;
            break;
    }

    hash ^= len;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    return hash;
}

/* ============ 哈希表实现 ============ */

/* 创建哈希表 */
hash_table_t* hash_table_create(int bucket_count, int max_size) {
    hash_table_t* ht = (hash_table_t*)malloc(sizeof(hash_table_t));
    if (ht == NULL) {
        LOG_ERROR("Failed to allocate hash table");
        return NULL;
    }

    ht->buckets = (hash_node_t**)calloc(bucket_count, sizeof(hash_node_t*));
    if (ht->buckets == NULL) {
        free(ht);
        LOG_ERROR("Failed to allocate buckets");
        return NULL;
    }

    ht->bucket_count = bucket_count;
    ht->size = 0;
    ht->max_size = max_size;
    ht->hash_func = murmurhash3;

    /* 创建分段锁 */
    ht->lock_count = 16;
    ht->locks = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * ht->lock_count);
    if (ht->locks == NULL) {
        free(ht->buckets);
        free(ht);
        LOG_ERROR("Failed to allocate locks");
        return NULL;
    }

    for (int i = 0; i < ht->lock_count; i++) {
        pthread_mutex_init(&ht->locks[i], NULL);
    }

    LOG_INFO("Hash table created with %d buckets", bucket_count);
    return ht;
}

/* 销毁哈希表 */
void hash_table_destroy(hash_table_t* ht) {
    if (ht == NULL) {
        return;
    }

    /* 释放所有节点 */
    for (int i = 0; i < ht->bucket_count; i++) {
        hash_node_t* node = ht->buckets[i];
        while (node != NULL) {
            hash_node_t* next = node->next;
            free(node);
            node = next;
        }
    }

    /* 释放锁 */
    for (int i = 0; i < ht->lock_count; i++) {
        pthread_mutex_destroy(&ht->locks[i]);
    }

    free(ht->locks);
    free(ht->buckets);
    free(ht);

    LOG_INFO("Hash table destroyed");
}

/* 获取锁索引 */
static int get_lock_index(hash_table_t* ht, uint32_t hash) {
    return hash % ht->lock_count;
}

/* 插入键值对 */
int hash_table_insert(hash_table_t* ht, const char* key, void* value) {
    if (ht == NULL || key == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (ht->size >= ht->max_size) {
        LOG_WARNING("Hash table is full");
        return GNTS_ERROR;
    }

    uint32_t hash = ht->hash_func(key);
    int bucket_index = hash % ht->bucket_count;
    int lock_index = get_lock_index(ht, hash);

    pthread_mutex_lock(&ht->locks[lock_index]);

    /* 查找是否已存在 */
    hash_node_t* node = ht->buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            node->value = value;
            pthread_mutex_unlock(&ht->locks[lock_index]);
            return GNTS_OK;
        }
        node = node->next;
    }

    /* 创建新节点 */
    node = (hash_node_t*)malloc(sizeof(hash_node_t));
    if (node == NULL) {
        pthread_mutex_unlock(&ht->locks[lock_index]);
        LOG_ERROR("Failed to allocate hash node");
        return GNTS_ERROR_MEMORY;
    }

    strncpy(node->key, key, STRING_MAX_LEN - 1);
    node->value = value;
    node->hash = hash;
    node->next = ht->buckets[bucket_index];
    ht->buckets[bucket_index] = node;
    ht->size++;

    pthread_mutex_unlock(&ht->locks[lock_index]);
    return GNTS_OK;
}

/* 查找值 */
void* hash_table_find(hash_table_t* ht, const char* key) {
    if (ht == NULL || key == NULL) {
        return NULL;
    }

    uint32_t hash = ht->hash_func(key);
    int bucket_index = hash % ht->bucket_count;
    int lock_index = get_lock_index(ht, hash);

    pthread_mutex_lock(&ht->locks[lock_index]);

    hash_node_t* node = ht->buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            void* value = node->value;
            pthread_mutex_unlock(&ht->locks[lock_index]);
            return value;
        }
        node = node->next;
    }

    pthread_mutex_unlock(&ht->locks[lock_index]);
    return NULL;
}

/* 删除键值对 */
int hash_table_delete(hash_table_t* ht, const char* key) {
    if (ht == NULL || key == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    uint32_t hash = ht->hash_func(key);
    int bucket_index = hash % ht->bucket_count;
    int lock_index = get_lock_index(ht, hash);

    pthread_mutex_lock(&ht->locks[lock_index]);

    hash_node_t* prev = NULL;
    hash_node_t* node = ht->buckets[bucket_index];

    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            if (prev == NULL) {
                ht->buckets[bucket_index] = node->next;
            } else {
                prev->next = node->next;
            }

            free(node);
            ht->size--;

            pthread_mutex_unlock(&ht->locks[lock_index]);
            return GNTS_OK;
        }

        prev = node;
        node = node->next;
    }

    pthread_mutex_unlock(&ht->locks[lock_index]);
    return GNTS_ERROR_NOT_FOUND;
}

/* 获取大小 */
int hash_table_size(hash_table_t* ht) {
    if (ht == NULL) {
        return 0;
    }

    return ht->size;
}

/* 遍历哈希表 */
int hash_table_foreach(hash_table_t* ht, hash_table_callback_t callback, void* user_data) {
    if (ht == NULL || callback == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    for (int i = 0; i < ht->bucket_count; i++) {
        int lock_index = i % ht->lock_count;
        pthread_mutex_lock(&ht->locks[lock_index]);

        hash_node_t* node = ht->buckets[i];
        while (node != NULL) {
            callback(node->key, node->value, user_data);
            node = node->next;
        }

        pthread_mutex_unlock(&ht->locks[lock_index]);
    }

    return GNTS_OK;
}

/* ============ 时间序列缓存实现 ============ */

/* 创建时间序列缓存 */
time_series_cache_t* time_series_cache_create(int capacity) {
    time_series_cache_t* cache = (time_series_cache_t*)malloc(sizeof(time_series_cache_t));
    if (cache == NULL) {
        LOG_ERROR("Failed to allocate time series cache");
        return NULL;
    }

    cache->points = (time_series_point_t*)malloc(sizeof(time_series_point_t) * capacity);
    if (cache->points == NULL) {
        free(cache);
        LOG_ERROR("Failed to allocate points");
        return NULL;
    }

    cache->capacity = capacity;
    cache->size = 0;
    cache->head = 0;
    cache->tail = 0;
    cache->start_time = 0;
    cache->end_time = 0;

    LOG_INFO("Time series cache created with capacity=%d", capacity);
    return cache;
}

/* 销毁时间序列缓存 */
void time_series_cache_destroy(time_series_cache_t* cache) {
    if (cache == NULL) {
        return;
    }

    if (cache->points != NULL) {
        free(cache->points);
    }

    free(cache);
    LOG_INFO("Time series cache destroyed");
}

/* 添加数据点 */
int time_series_cache_add(time_series_cache_t* cache, timestamp_t time, double value) {
    if (cache == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (cache->size >= cache->capacity) {
        /* 移除最旧的数据 */
        cache->tail = (cache->tail + 1) % cache->capacity;
        cache->size--;
    }

    int index = cache->head;
    cache->points[index].timestamp = time;
    cache->points[index].value = value;

    cache->head = (cache->head + 1) % cache->capacity;
    cache->size++;

    /* 更新时间范围 */
    if (cache->start_time == 0 || time < cache->start_time) {
        cache->start_time = time;
    }
    if (time > cache->end_time) {
        cache->end_time = time;
    }

    return GNTS_OK;
}

/* 查询时间范围内的数据 */
int time_series_cache_query(time_series_cache_t* cache, timestamp_t start,
                             timestamp_t end, time_series_point_t** points, int* count) {
    if (cache == NULL || points == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    /* 计算范围内的数据数量 */
    int match_count = 0;
    for (int i = 0; i < cache->size; i++) {
        int index = (cache->tail + i) % cache->capacity;
        if (cache->points[index].timestamp >= start &&
            cache->points[index].timestamp <= end) {
            match_count++;
        }
    }

    if (match_count == 0) {
        *points = NULL;
        *count = 0;
        return GNTS_OK;
    }

    /* 分配内存 */
    *points = (time_series_point_t*)malloc(sizeof(time_series_point_t) * match_count);
    if (*points == NULL) {
        LOG_ERROR("Failed to allocate points");
        return GNTS_ERROR_MEMORY;
    }

    /* 复制数据 */
    int index = 0;
    for (int i = 0; i < cache->size; i++) {
        int pos = (cache->tail + i) % cache->capacity;
        if (cache->points[pos].timestamp >= start &&
            cache->points[pos].timestamp <= end) {
            (*points)[index++] = cache->points[pos];
        }
    }

    *count = match_count;
    return GNTS_OK;
}

/* 获取最新值 */
double time_series_cache_get_latest(time_series_cache_t* cache) {
    if (cache == NULL || cache->size == 0) {
        return 0.0;
    }

    int index = (cache->head - 1 + cache->capacity) % cache->capacity;
    return cache->points[index].value;
}

/* 清空缓存 */
void time_series_cache_clear(time_series_cache_t* cache) {
    if (cache == NULL) {
        return;
    }

    cache->size = 0;
    cache->head = 0;
    cache->tail = 0;
    cache->start_time = 0;
    cache->end_time = 0;
}

/* ============ 滑动窗口实现 ============ */

/* 重新计算 min/max（当旧极值被驱逐时调用） */
static void recalc_min_max(sliding_window_t* window) {
    if (window->size == 0) {
        window->min = 0.0;
        window->max = 0.0;
        return;
    }

    double min_val = window->data[window->head];
    double max_val = window->data[window->head];
    for (int i = 1; i < window->size; i++) {
        int index = (window->head + i) % window->capacity;
        double val = window->data[index];
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }
    window->min = min_val;
    window->max = max_val;
    window->min_max_valid = 1;
}

/* 创建滑动窗口 */
sliding_window_t* sliding_window_create(int capacity) {
    sliding_window_t* window = (sliding_window_t*)malloc(sizeof(sliding_window_t));
    if (window == NULL) {
        LOG_ERROR("Failed to allocate sliding window");
        return NULL;
    }

    window->data = (double*)malloc(sizeof(double) * capacity);
    if (window->data == NULL) {
        free(window);
        LOG_ERROR("Failed to allocate data");
        return NULL;
    }

    window->capacity = capacity;
    window->size = 0;
    window->head = 0;
    window->sum = 0.0;
    window->min = 0.0;
    window->max = 0.0;
    window->min_max_valid = 0;

    LOG_INFO("Sliding window created with capacity=%d", capacity);
    return window;
}

/* 销毁滑动窗口 */
void sliding_window_destroy(sliding_window_t* window) {
    if (window == NULL) {
        return;
    }

    if (window->data != NULL) {
        free(window->data);
    }

    free(window);
    LOG_INFO("Sliding window destroyed");
}

/* 添加数据 */
int sliding_window_add(sliding_window_t* window, double value) {
    if (window == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    int evicted_minmax = 0;

    if (window->size >= window->capacity) {
        /* 移除最旧的数据 */
        double old_value = window->data[window->head];
        window->sum -= old_value;

        /* 检查被驱逐的值是否为当前 min/max */
        if (window->min_max_valid) {
            if (old_value <= window->min || old_value >= window->max) {
                evicted_minmax = 1;
            }
        }

        window->head = (window->head + 1) % window->capacity;
        window->size--;
    }

    int index = (window->head + window->size) % window->capacity;
    window->data[index] = value;
    window->sum += value;
    window->size++;

    /* 更新最小值和最大值 */
    if (window->size == 1 || !window->min_max_valid) {
        window->min = value;
        window->max = value;
        window->min_max_valid = 1;
    } else {
        if (evicted_minmax) {
            /* 极值被驱逐，需重新计算 */
            recalc_min_max(window);
        } else {
            /* 快速路径：只需与新值比较 */
            if (value < window->min) {
                window->min = value;
            }
            if (value > window->max) {
                window->max = value;
            }
        }
    }

    return GNTS_OK;
}

/* 获取平均值 */
double sliding_window_get_avg(sliding_window_t* window) {
    if (window == NULL || window->size == 0) {
        return 0.0;
    }

    return window->sum / window->size;
}

/* 获取最小值 */
double sliding_window_get_min(sliding_window_t* window) {
    if (window == NULL || window->size == 0) {
        return 0.0;
    }

    return window->min;
}

/* 获取最大值 */
double sliding_window_get_max(sliding_window_t* window) {
    if (window == NULL || window->size == 0) {
        return 0.0;
    }

    return window->max;
}

/* 获取标准差 */
double sliding_window_get_std(sliding_window_t* window) {
    if (window == NULL || window->size <= 1) {
        return 0.0;
    }

    double avg = sliding_window_get_avg(window);
    double sum_sq = 0.0;

    for (int i = 0; i < window->size; i++) {
        int index = (window->head + i) % window->capacity;
        double diff = window->data[index] - avg;
        sum_sq += diff * diff;
    }

    return sqrt(sum_sq / window->size);
}

/* 清空窗口 */
void sliding_window_clear(sliding_window_t* window) {
    if (window == NULL) {
        return;
    }

    window->size = 0;
    window->head = 0;
    window->sum = 0.0;
    window->min = 0.0;
    window->max = 0.0;
    window->min_max_valid = 0;
}