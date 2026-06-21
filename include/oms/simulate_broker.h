/*
 * GNTS - 观澜 · Nexus Trading System
 * 模拟券商接口实现
 */

#ifndef GNTS_OMS_SIMULATE_BROKER_H
#define GNTS_OMS_SIMULATE_BROKER_H

#include "oms/order_manager.h"
#include "common/high_performance.h"
#include <pthread.h>

/* 模拟券商配置 */
typedef struct {
    double initial_cash;           /* 初始资金 */
    double commission_rate;        /* 手续费率 */
    double slippage_rate;          /* 滑点率 */
    int latency_ms;                /* 模拟延迟（毫秒） */
    int fill_probability;          /* 成交概率（0-100） */
} simulate_broker_config_t;

/* 模拟账户 */
typedef struct {
    double cash;
    double frozen_cash;
    double total_asset;
    double profit_loss;
} simulate_account_t;

/* 模拟持仓节点 */
typedef struct simulate_position_node {
    char symbol[SYMBOL_MAX_LEN];
    int64_t long_position;       /* 多头持仓 */
    int64_t short_position;      /* 空头持仓 */
    int64_t long_available;     /* 多头可用 */
    int64_t short_available;    /* 空头可用 */
    double long_cost;           /* 多头成本 */
    double short_cost;          /* 空头成本 */
    double market_value;         /* 市值 */
    struct simulate_position_node* next;
} simulate_position_node_t;

/* 模拟订单节点 */
typedef struct simulate_order_node {
    char order_id[64];
    char symbol[SYMBOL_MAX_LEN];
    order_direction_t direction;
    order_type_t type;
    double price;
    int64_t quantity;
    int64_t filled_quantity;
    double avg_price;
    order_status_t status;
    timestamp_t create_time;
    timestamp_t update_time;
    struct simulate_order_node* next;
} simulate_order_node_t;

/* 模拟成交节点 */
typedef struct simulate_trade_node {
    char trade_id[64];
    char order_id[64];
    char symbol[SYMBOL_MAX_LEN];
    order_direction_t direction;
    double price;
    int64_t quantity;
    timestamp_t trade_time;
    struct simulate_trade_node* next;
} simulate_trade_node_t;

/* 模拟券商内部结构 */
typedef struct {
    /* 基础接口 */
    int connected;
    simulate_broker_config_t config;

    /* 账户信息 */
    simulate_account_t account;

    /* 持仓管理 */
    simulate_position_node_t* positions;
    int position_count;
    hash_table_t* position_table;    /* 快速查找 */

    /* 订单管理 */
    simulate_order_node_t* orders;
    int order_count;
    hash_table_t* order_table;       /* 快速查找 */

    /* 成交记录 */
    simulate_trade_node_t* trades;
    int trade_count;
    hash_table_t* trade_table;       /* 快速查找 */

    /* 线程安全 */
    pthread_mutex_t mutex;

    /* 市场数据（简单模拟） */
    hash_table_t* market_data;       /* 存储最新价格 */
} simulate_broker_t;

/* 创建模拟券商 */
broker_interface_t* create_simulate_broker(const simulate_broker_config_t* config);

/* 创建默认配置的模拟券商 */
broker_interface_t* create_simulate_broker_default(void);

/* 设置模拟市场数据 */
int simulate_broker_set_market_data(broker_interface_t* broker,
                                   const char* symbol, double price);

/* 模拟订单成交（内部使用） */
int simulate_broker_process_order(simulate_broker_t* broker,
                                  simulate_order_node_t* order);

#endif /* GNTS_OMS_SIMULATE_BROKER_H */