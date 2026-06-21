/*
 * GNTS - 观澜 · Nexus Trading System
 * 公共类型定义
 */

#ifndef GNTS_COMMON_TYPES_H
#define GNTS_COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* 动态数组扩容宏 */
#define DYNAMIC_ARRAY_ENSURE_CAPACITY(arr, count, capacity, elem_size, initial) \
    do { \
        if (arr == NULL) { \
            (capacity) = (initial); \
            (arr) = malloc((elem_size) * (capacity)); \
            if ((arr) == NULL) { \
                return GNTS_ERROR_MEMORY; \
            } \
        } else if ((count) >= (capacity)) { \
            (capacity) = (capacity) * 2; \
            void* _new_arr = realloc((arr), (elem_size) * (capacity)); \
            if (_new_arr == NULL) { \
                return GNTS_ERROR_MEMORY; \
            } \
            (arr) = _new_arr; \
        } \
    } while(0)

/* 动态数组扩容宏（不返回，用于void函数） */
#define DYNAMIC_ARRAY_ENSURE_CAPACITY_VOID(arr, count, capacity, elem_size, initial) \
    do { \
        if (arr == NULL) { \
            (capacity) = (initial); \
            (arr) = malloc((elem_size) * (capacity)); \
        } else if ((count) >= (capacity)) { \
            (capacity) = (capacity) * 2; \
            void* _new_arr = realloc((arr), (elem_size) * (capacity)); \
            if (_new_arr != NULL) { \
                (arr) = _new_arr; \
            } \
        } \
    } while(0)

/* 错误码定义 */
typedef enum {
    GNTS_OK = 0,
    GNTS_ERROR = -1,
    GNTS_ERROR_INVALID_PARAM = -2,
    GNTS_ERROR_MEMORY = -3,
    GNTS_ERROR_NOT_FOUND = -4,
    GNTS_ERROR_TIMEOUT = -5,
    GNTS_ERROR_NETWORK = -6,
    GNTS_ERROR_DATABASE = -7,
    GNTS_ERROR_PERMISSION = -8,
} gnts_error_t;

/* 时间戳类型 */
typedef int64_t timestamp_t;

/* 股票代码最大长度 */
#define SYMBOL_MAX_LEN 16

/* 字符串最大长度 */
#define STRING_MAX_LEN 256

/* 买卖方向 */
typedef enum {
    ORDER_BUY = 1,
    ORDER_SELL = 2,
} order_direction_t;

/* 订单类型 */
typedef enum {
    ORDER_TYPE_LIMIT = 1,      /* 限价单 */
    ORDER_TYPE_MARKET = 2,     /* 市价单 */
    ORDER_TYPE_STOP = 3,       /* 止损单 */
    ORDER_TYPE_STOP_LIMIT = 4, /* 止损限价单 */
} order_type_t;

/* 订单状态 */
typedef enum {
    ORDER_STATUS_PENDING = 0,      /* 待提交 */
    ORDER_STATUS_SUBMITTED = 1,    /* 已提交 */
    ORDER_STATUS_PARTIAL_FILLED = 2, /* 部分成交 */
    ORDER_STATUS_FILLED = 3,       /* 完全成交 */
    ORDER_STATUS_CANCELLED = 4,    /* 已撤单 */
    ORDER_STATUS_REJECTED = 5,     /* 已拒绝 */
} order_status_t;

/* K线周期 */
typedef enum {
    BAR_PERIOD_TICK = 0,      /* Tick */
    BAR_PERIOD_1MIN = 60,     /* 1分钟 */
    BAR_PERIOD_5MIN = 300,    /* 5分钟 */
    BAR_PERIOD_15MIN = 900,   /* 15分钟 */
    BAR_PERIOD_30MIN = 1800,  /* 30分钟 */
    BAR_PERIOD_1HOUR = 3600,  /* 1小时 */
    BAR_PERIOD_1DAY = 86400,  /* 日线 */
    BAR_PERIOD_1WEEK = 604800, /* 周线 */
    BAR_PERIOD_1MONTH = 2592000, /* 月线 */
} bar_period_t;

/* 市场类型 */
typedef enum {
    MARKET_STOCK = 1,   /* 股票 */
    MARKET_FUTURES = 2, /* 期货 */
    MARKET_OPTIONS = 3, /* 期权 */
    MARKET_CRYPTO = 4,  /* 数字货币 */
} market_type_t;

/* 时间工具函数 */
static inline timestamp_t get_current_timestamp(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (timestamp_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* 时间戳转换为字符串 */
static inline void timestamp_to_string(timestamp_t ts, char* buf, size_t len) {
    time_t time = ts / 1000000000LL;
    struct tm* tm_info = localtime(&time);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
}

#endif /* GNTS_COMMON_TYPES_H */