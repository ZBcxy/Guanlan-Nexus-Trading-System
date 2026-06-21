/*
 * GNTS - 观澜 · Nexus Trading System
 * 交易执行系统（OMS）接口定义
 */

#ifndef GNTS_OMS_ORDER_MANAGER_H
#define GNTS_OMS_ORDER_MANAGER_H

#include "common/types.h"
#include "common/structures.h"

/* ============ 券商接口 ============ */

/* 券商类型 */
typedef enum {
    BROKER_XTP = 1,      /* XTP */
    BROKER_CTP = 2,      /* CTP */
    BROKER_SIMULATE = 3, /* 模拟券商 */
    BROKER_CUSTOM = 99,  /* 自定义券商 */
} broker_type_t;

/* 券商配置 */
typedef struct {
    broker_type_t type;
    char broker_id[STRING_MAX_LEN];
    char account[STRING_MAX_LEN];
    char password[STRING_MAX_LEN];
    char front_addr[STRING_MAX_LEN];
    int front_port;
    char app_id[STRING_MAX_LEN];
    char auth_code[STRING_MAX_LEN];
} broker_config_t;

/* 券商接口 */
typedef struct broker_interface {
    int (*connect)(struct broker_interface* self, const broker_config_t* config);
    void (*disconnect)(struct broker_interface* self);
    int (*is_connected)(struct broker_interface* self);

    /* 账户查询 */
    int (*query_account)(struct broker_interface* self, account_info_t* account);
    int (*query_position)(struct broker_interface* self, const char* symbol, position_t* position);
    int (*query_positions)(struct broker_interface* self, position_t** positions, int* count);

    /* 订单操作 */
    int (*send_order)(struct broker_interface* self, const order_request_t* request,
                      order_response_t* response);
    int (*cancel_order)(struct broker_interface* self, const char* order_id);
    int (*query_order)(struct broker_interface* self, const char* order_id,
                       order_response_t* response);
    int (*query_orders)(struct broker_interface* self, order_response_t** orders, int* count);

    /* 成交查询 */
    int (*query_trade)(struct broker_interface* self, const char* trade_id,
                       trade_record_t* trade);
    int (*query_trades)(struct broker_interface* self, trade_record_t** trades, int* count);

    /* 行情订阅 */
    int (*subscribe)(struct broker_interface* self, const char* symbol);
    int (*unsubscribe)(struct broker_interface* self, const char* symbol);

    void* private_data;
} broker_interface_t;

/* ============ 订单管理器 ============ */

/* 订单回调 */
typedef void (*order_callback_t)(const order_response_t* order, void* user_data);
typedef void (*trade_callback_t)(const trade_record_t* trade, void* user_data);

/* 订单管理器 */
typedef struct {
    broker_interface_t* broker;
    order_response_t* orders;
    int order_count;
    int max_orders;
    trade_record_t* trades;
    int trade_count;
    int max_trades;
    order_callback_t order_callback;
    trade_callback_t trade_callback;
    void* callback_user_data;
    int is_running;
} order_manager_t;

/* 创建订单管理器 */
order_manager_t* order_manager_create(void);

/* 销毁订单管理器 */
void order_manager_destroy(order_manager_t* manager);

/* 设置券商接口 */
int order_manager_set_broker(order_manager_t* manager, broker_interface_t* broker);

/* 连接券商 */
int order_manager_connect(order_manager_t* manager, const broker_config_t* config);

/* 断开券商 */
int order_manager_disconnect(order_manager_t* manager);

/* 发送订单 */
int order_manager_send_order(order_manager_t* manager, const order_request_t* request,
                             order_response_t* response);

/* 撤销订单 */
int order_manager_cancel_order(order_manager_t* manager, const char* order_id);

/* 查询订单 */
int order_manager_query_order(order_manager_t* manager, const char* order_id,
                              order_response_t* response);

/* 查询所有订单 */
int order_manager_query_orders(order_manager_t* manager, order_response_t** orders, int* count);

/* 查询成交 */
int order_manager_query_trade(order_manager_t* manager, const char* trade_id,
                              trade_record_t* trade);

/* 查询所有成交 */
int order_manager_query_trades(order_manager_t* manager, trade_record_t** trades, int* count);

/* 设置回调 */
int order_manager_set_callback(order_manager_t* manager,
                                order_callback_t order_cb,
                                trade_callback_t trade_cb,
                                void* user_data);

/* 启动订单管理器 */
int order_manager_start(order_manager_t* manager);

/* 停止订单管理器 */
int order_manager_stop(order_manager_t* manager);

/* ============ 仓位管理器 ============ */

/* 仓位管理器 */
typedef struct {
    position_t* positions;
    int position_count;
    int max_positions;
    account_info_t account;
} position_manager_t;

/* 创建仓位管理器 */
position_manager_t* position_manager_create(void);

/* 销毁仓位管理器 */
void position_manager_destroy(position_manager_t* manager);

/* 更新持仓 */
int position_manager_update(position_manager_t* manager, const position_t* position);

/* 查询持仓 */
int position_manager_query(position_manager_t* manager, const char* symbol, position_t* position);

/* 查询所有持仓 */
int position_manager_query_all(position_manager_t* manager, position_t** positions, int* count);

/* 更新账户信息 */
int position_manager_update_account(position_manager_t* manager, const account_info_t* account);

/* 查询账户信息 */
int position_manager_query_account(position_manager_t* manager, account_info_t* account);

/* 计算市值 */
double position_manager_calculate_market_value(position_manager_t* manager);

/* 计算盈亏 */
double position_manager_calculate_profit_loss(position_manager_t* manager);

/* ============ 算法交易 ============ */

/* 算法交易类型 */
typedef enum {
    ALGO_TYPE_TWAP = 1,    /* 时间加权平均价格 */
    ALGO_TYPE_VWAP = 2,    /* 成交量加权平均价格 */
    ALGO_TYPE_ICEBERG = 3, /* 冰山指令 */
    ALGO_TYPE_SNIPER = 4,  /* 狙击手 */
    ALGO_TYPE_CUSTOM = 99, /* 自定义算法 */
} algo_type_t;

/* 算法交易配置 */
typedef struct {
    algo_type_t type;
    char symbol[SYMBOL_MAX_LEN];
    order_direction_t direction;
    int64_t total_quantity;
    double price_limit;
    timestamp_t start_time;
    timestamp_t end_time;
    int slice_count;              /* 拆单数量 */
    double slice_interval;        /* 拆单间隔（秒） */
    double participation_rate;    /* 参与率（VWAP用） */
    double display_quantity;      /* 显示数量（冰山用） */
    void* custom_params;
} algo_config_t;

/* 算法交易器 */
typedef struct {
    algo_config_t config;
    order_manager_t* order_manager;
    int64_t filled_quantity;
    double filled_value;
    int slice_filled;
    int is_running;
} algo_trader_t;

/* 创建算法交易器 */
algo_trader_t* algo_trader_create(const algo_config_t* config);

/* 销毁算法交易器 */
void algo_trader_destroy(algo_trader_t* trader);

/* 设置订单管理器 */
int algo_trader_set_order_manager(algo_trader_t* trader, order_manager_t* manager);

/* 启动算法交易 */
int algo_trader_start(algo_trader_t* trader);

/* 停止算法交易 */
int algo_trader_stop(algo_trader_t* trader);

/* 获取执行进度 */
double algo_trader_get_progress(algo_trader_t* trader);

/* 获取平均成交价 */
double algo_trader_get_avg_price(algo_trader_t* trader);

#endif /* GNTS_OMS_ORDER_MANAGER_H */