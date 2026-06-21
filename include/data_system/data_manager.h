/*
 * GNTS - 观澜 · Nexus Trading System
 * 数据系统接口定义
 */

#ifndef GNTS_DATA_SYSTEM_DATA_MANAGER_H
#define GNTS_DATA_SYSTEM_DATA_MANAGER_H

#include "common/types.h"
#include "common/structures.h"

/* ============ 数据源接口 ============ */

/* 数据源类型 */
typedef enum {
    DATA_SOURCE_AKSHARE = 1,    /* AKShare */
    DATA_SOURCE_TUSHARE = 2,   /* Tushare */
    DATA_SOURCE_XTP = 3,       /* XTP */
    DATA_SOURCE_CTP = 4,       /* CTP */
    DATA_SOURCE_CUSTOM = 99,   /* 自定义数据源 */
} data_source_type_t;

/* 数据源配置 */
typedef struct {
    data_source_type_t type;
    char host[STRING_MAX_LEN];
    int port;
    char username[STRING_MAX_LEN];
    char password[STRING_MAX_LEN];
    char token[STRING_MAX_LEN];
} data_source_config_t;

/* 数据源接口 */
typedef struct data_source_interface {
    int (*connect)(struct data_source_interface* self);
    void (*disconnect)(struct data_source_interface* self);
    int (*subscribe)(struct data_source_interface* self, const char* symbol);
    int (*unsubscribe)(struct data_source_interface* self, const char* symbol);
    int (*query_history)(struct data_source_interface* self, const char* symbol,
                         timestamp_t start, timestamp_t end, bar_period_t period,
                         bar_data_t** bars, int* count);
    int (*query_tick)(struct data_source_interface* self, const char* symbol,
                      timestamp_t start, timestamp_t end,
                      tick_data_t** ticks, int* count);
    int (*query_financial)(struct data_source_interface* self, const char* symbol,
                           financial_data_t* data);
    void* private_data;
} data_source_interface_t;

/* ============ 数据存储接口 ============ */

/* 数据存储类型 */
typedef enum {
    STORAGE_TYPE_MEMORY = 1,      /* 内存存储 */
    STORAGE_TYPE_SQLITE = 2,      /* SQLite */
    STORAGE_TYPE_INFLUXDB = 3,    /* InfluxDB */
    STORAGE_TYPE_CLICKHOUSE = 4,  /* ClickHouse */
} storage_type_t;

/* 数据存储配置 */
typedef struct {
    storage_type_t type;
    char path[STRING_MAX_LEN];
    char host[STRING_MAX_LEN];
    int port;
    char database[STRING_MAX_LEN];
    char username[STRING_MAX_LEN];
    char password[STRING_MAX_LEN];
} storage_config_t;

/* 数据存储接口 */
typedef struct {
    int (*init)(const storage_config_t* config);
    void (*shutdown)(void);
    int (*save_bar)(const bar_data_t* bar);
    int (*save_bars)(const bar_data_t* bars, int count);
    int (*save_tick)(const tick_data_t* tick);
    int (*save_ticks)(const tick_data_t* ticks, int count);
    int (*query_bars)(const char* symbol, timestamp_t start, timestamp_t end,
                      bar_period_t period, bar_data_t** bars, int* count);
    int (*query_ticks)(const char* symbol, timestamp_t start, timestamp_t end,
                       tick_data_t** ticks, int* count);
    int (*delete_bars)(const char* symbol, timestamp_t start, timestamp_t end);
    int (*delete_ticks)(const char* symbol, timestamp_t start, timestamp_t end);
} storage_interface_t;

/* ============ 数据管理器 ============ */

/* 行情回调函数 */
typedef void (*tick_callback_t)(const tick_data_t* tick, void* user_data);
typedef void (*bar_callback_t)(const bar_data_t* bar, void* user_data);

/* 数据管理器 */
typedef struct {
    data_source_interface_t* data_source;
    storage_interface_t* storage;
    tick_callback_t tick_callback;
    bar_callback_t bar_callback;
    void* callback_user_data;
    int is_running;
} data_manager_t;

/* 创建数据管理器 */
data_manager_t* data_manager_create(void);

/* 销毁数据管理器 */
void data_manager_destroy(data_manager_t* manager);

/* 添加数据源 */
int data_manager_add_source(data_manager_t* manager, data_source_interface_t* source);

/* 设置存储 */
int data_manager_set_storage(data_manager_t* manager, storage_interface_t* storage);

/* 订阅行情 */
int data_manager_subscribe(data_manager_t* manager, const char* symbol);

/* 取消订阅 */
int data_manager_unsubscribe(data_manager_t* manager, const char* symbol);

/* 查询历史K线 */
int data_manager_query_bars(data_manager_t* manager, const char* symbol,
                            timestamp_t start, timestamp_t end,
                            bar_period_t period, bar_data_t** bars, int* count);

/* 查询历史Tick */
int data_manager_query_ticks(data_manager_t* manager, const char* symbol,
                             timestamp_t start, timestamp_t end,
                             tick_data_t** ticks, int* count);

/* 查询财务数据 */
int data_manager_query_financial(data_manager_t* manager, const char* symbol,
                                 financial_data_t* data);

/* 设置行情回调 */
int data_manager_set_callback(data_manager_t* manager,
                              tick_callback_t tick_cb,
                              bar_callback_t bar_cb,
                              void* user_data);

/* 启动数据管理器 */
int data_manager_start(data_manager_t* manager);

/* 停止数据管理器 */
int data_manager_stop(data_manager_t* manager);

/* ============ 数据清洗工具 ============ */

/* 复权类型 */
typedef enum {
    ADJUST_TYPE_NONE = 0,       /* 不复权 */
    ADJUST_TYPE_FRONT = 1,      /* 前复权 */
    ADJUST_TYPE_BACK = 2,       /* 后复权 */
} adjust_type_t;

/* 除权除息事件 */
typedef struct {
    timestamp_t date;              /* 除权除息日期 */
    double cash_dividend;          /* 每股现金分红 */
    double bonus_share_ratio;      /* 送股比例（每10股送N股，存储为N/10） */
    double rights_issue_ratio;     /* 配股比例 */
    double rights_issue_price;     /* 配股价格 */
} dividend_event_t;

/* 数据清洗器（不透明类型） */
typedef struct data_cleaner data_cleaner_t;

/* 创建数据清洗器 */
data_cleaner_t* data_cleaner_create(void);

/* 销毁数据清洗器 */
void data_cleaner_destroy(data_cleaner_t* cleaner);

/* 注册除权除息事件 */
int data_cleaner_add_dividend(data_cleaner_t* cleaner, const dividend_event_t* event);

/* 清除已注册的除权除息事件 */
void data_cleaner_clear_dividends(data_cleaner_t* cleaner);

/* 处理停牌数据 */
int data_cleaner_handle_suspension(data_cleaner_t* cleaner, bar_data_t* bars, int count);

/* 复权处理 */
int data_cleaner_adjust_price(data_cleaner_t* cleaner, bar_data_t* bars, int count, adjust_type_t type);

/* 去除异常数据 */
int data_cleaner_remove_outliers(data_cleaner_t* cleaner, bar_data_t* bars, int count, double threshold);

/* 填充缺失数据 */
int data_cleaner_fill_missing(data_cleaner_t* cleaner, bar_data_t* bars, int count);

/* ============ 内存存储工厂函数 ============ */

/* 获取内存存储接口（全局单例） */
storage_interface_t* get_memory_storage(void);

#endif /* GNTS_DATA_SYSTEM_DATA_MANAGER_H */