/*
 * GNTS - 观澜 · Nexus Trading System
 * 高性能数据结构
 */

#ifndef GNTS_COMMON_HIGH_PERFORMANCE_H
#define GNTS_COMMON_HIGH_PERFORMANCE_H

#include "common/types.h"
#include <pthread.h>

/* ============ 高性能哈希表 ============ */

/* 哈希表节点 */
typedef struct hash_node {
    char key[STRING_MAX_LEN];
    void* value;
    struct hash_node* next;
    uint32_t hash;
} hash_node_t;

/* 高性能哈希表 */
typedef struct {
    hash_node_t** buckets;
    int bucket_count;
    int size;
    int max_size;
    uint32_t (*hash_func)(const char* key);
    pthread_mutex_t* locks;     /* 分段锁 */
    int lock_count;
} hash_table_t;

/* 创建哈希表 */
hash_table_t* hash_table_create(int bucket_count, int max_size);

/* 销毁哈希表 */
void hash_table_destroy(hash_table_t* ht);

/* 插入键值对 */
int hash_table_insert(hash_table_t* ht, const char* key, void* value);

/* 查找值 */
void* hash_table_find(hash_table_t* ht, const char* key);

/* 删除键值对 */
int hash_table_delete(hash_table_t* ht, const char* key);

/* 获取大小 */
int hash_table_size(hash_table_t* ht);

/* 遍历哈希表 */
typedef void (*hash_table_callback_t)(const char* key, void* value, void* user_data);
int hash_table_foreach(hash_table_t* ht, hash_table_callback_t callback, void* user_data);

/* ============ 高性能时间序列缓存 ============ */

/* 时间序列数据点 */
typedef struct {
    timestamp_t timestamp;
    double value;
} time_series_point_t;

/* 时间序列缓存 */
typedef struct {
    time_series_point_t* points;
    int capacity;
    int size;
    int head;
    int tail;
    timestamp_t start_time;
    timestamp_t end_time;
} time_series_cache_t;

/* 创建时间序列缓存 */
time_series_cache_t* time_series_cache_create(int capacity);

/* 销毁时间序列缓存 */
void time_series_cache_destroy(time_series_cache_t* cache);

/* 添加数据点 */
int time_series_cache_add(time_series_cache_t* cache, timestamp_t time, double value);

/* 查询时间范围内的数据 */
int time_series_cache_query(time_series_cache_t* cache, timestamp_t start,
                             timestamp_t end, time_series_point_t** points, int* count);

/* 获取最新值 */
double time_series_cache_get_latest(time_series_cache_t* cache);

/* 清空缓存 */
void time_series_cache_clear(time_series_cache_t* cache);

/* ============ 高性能订单簿 ============ */

/* 价格层级 */
typedef struct {
    double price;
    int64_t volume;
    int order_count;
} price_level_t;

/* 订单簿 */
typedef struct {
    price_level_t* bid_levels;    /* 买盘层级 */
    price_level_t* ask_levels;    /* 卖盘层级 */
    int bid_count;                /* 买盘层级数量 */
    int ask_count;                /* 卖盘层级数量 */
    int max_levels;               /* 最大层级数量 */
    double best_bid;              /* 最佳买价 */
    double best_ask;              /* 最佳卖价 */
    timestamp_t update_time;
} order_book_t;

/* 创建订单簿 */
order_book_t* order_book_create(int max_levels);

/* 销毁订单簿 */
void order_book_destroy(order_book_t* ob);

/* 更新买盘 */
int order_book_update_bid(order_book_t* ob, double price, int64_t volume);

/* 更新卖盘 */
int order_book_update_ask(order_book_t* ob, double price, int64_t volume);

/* 获取最佳买价 */
double order_book_get_best_bid(order_book_t* ob);

/* 获取最佳卖价 */
double order_book_get_best_ask(order_book_t* ob);

/* 获取买盘深度 */
int order_book_get_bid_depth(order_book_t* ob, price_level_t** levels, int* count);

/* 获取卖盘深度 */
int order_book_get_ask_depth(order_book_t* ob, price_level_t** levels, int* count);

/* ============ 高性能滑动窗口 ============ */

/* 滑动窗口 */
typedef struct {
    double* data;
    int capacity;
    int size;
    int head;
    double sum;
    double min;
    double max;
    int min_max_valid;   /* min/max 值是否有效 */
} sliding_window_t;

/* 创建滑动窗口 */
sliding_window_t* sliding_window_create(int capacity);

/* 销毁滑动窗口 */
void sliding_window_destroy(sliding_window_t* window);

/* 添加数据 */
int sliding_window_add(sliding_window_t* window, double value);

/* 获取平均值 */
double sliding_window_get_avg(sliding_window_t* window);

/* 获取最小值 */
double sliding_window_get_min(sliding_window_t* window);

/* 获取最大值 */
double sliding_window_get_max(sliding_window_t* window);

/* 获取标准差 */
double sliding_window_get_std(sliding_window_t* window);

/* 清空窗口 */
void sliding_window_clear(sliding_window_t* window);

#endif /* GNTS_COMMON_HIGH_PERFORMANCE_H */