/*
 * GNTS - 观澜 · Nexus Trading System
 * 风险控制与监控系统接口定义
 */

#ifndef GNTS_RISK_RISK_MANAGER_H
#define GNTS_RISK_RISK_MANAGER_H

#include "common/types.h"
#include "common/structures.h"

/* ============ 风控规则 ============ */

/* 风控规则类型 */
typedef enum {
    RISK_RULE_POSITION_LIMIT = 1,      /* 持仓限制 */
    RISK_RULE_ORDER_LIMIT = 2,         /* 订单限制 */
    RISK_RULE_LOSS_LIMIT = 3,          /* 亏损限制 */
    RISK_RULE_TRADE_LIMIT = 4,         /* 交易次数限制 */
    RISK_RULE_BLACKLIST = 5,           /* 黑名单 */
    RISK_RULE_WHITELIST = 6,           /* 白名单 */
    RISK_RULE_PRICE_DEVIATION = 7,     /* 价格偏离 */
    RISK_RULE_VOLUME_LIMIT = 8,        /* 成交量限制 */
    RISK_RULE_CUSTOM = 99,             /* 自定义规则 */
} risk_rule_type_t;

/* 风控动作 */
typedef enum {
    RISK_ACTION_REJECT = 1,    /* 拒绝 */
    RISK_ACTION_WARN = 2,      /* 警告 */
    RISK_ACTION_LOG = 3,       /* 记录 */
    RISK_ACTION_BLOCK = 4,     /* 阻断 */
} risk_action_t;

/* 风控规则（前向声明） */
typedef struct risk_rule risk_rule_t;

/* 风控规则 */
struct risk_rule {
    char name[STRING_MAX_LEN];
    risk_rule_type_t type;
    risk_action_t action;
    int enabled;
    void* params;
    int (*check)(risk_rule_t* rule, const void* context);
};

/* ============ 风控上下文 ============ */

/* 风控检查上下文 */
typedef struct {
    const order_request_t* order;
    const position_t* position;
    const account_info_t* account;
    const bar_data_t* bar;          /* 当前市场行情（用于价格偏离检查等） */
    const char* strategy_id;
    timestamp_t timestamp;
    void* user_data;
} risk_context_t;

/* ============ 风控管理器 ============ */

/* 风控检查结果 */
typedef struct {
    int passed;                         /* 是否通过 */
    risk_rule_t* failed_rule;           /* 失败的规则 */
    char message[STRING_MAX_LEN];       /* 消息 */
} risk_check_result_t;

/* 风控管理器 */
typedef struct {
    risk_rule_t* rules;
    int rule_count;
    int max_rules;
    int is_enabled;
} risk_manager_t;

/* 创建风控管理器 */
risk_manager_t* risk_manager_create(void);

/* 销毁风控管理器 */
void risk_manager_destroy(risk_manager_t* manager);

/* 添加风控规则 */
int risk_manager_add_rule(risk_manager_t* manager, const risk_rule_t* rule);

/* 移除风控规则 */
int risk_manager_remove_rule(risk_manager_t* manager, const char* rule_name);

/* 启用风控规则 */
int risk_manager_enable_rule(risk_manager_t* manager, const char* rule_name);

/* 禁用风控规则 */
int risk_manager_disable_rule(risk_manager_t* manager, const char* rule_name);

/* 检查订单（事前风控） */
int risk_manager_check_order(risk_manager_t* manager, const risk_context_t* context,
                             risk_check_result_t* result);

/* 启用风控 */
int risk_manager_enable(risk_manager_t* manager);

/* 禁用风控 */
int risk_manager_disable(risk_manager_t* manager);

/* ============ 预定义风控规则 ============ */

/* 持仓限制规则 */
risk_rule_t* risk_rule_create_position_limit(const char* name, double max_position_ratio);

/* 订单限制规则 */
risk_rule_t* risk_rule_create_order_limit(const char* name, int max_orders_per_day,
                                          double max_order_value);

/* 亏损限制规则 */
risk_rule_t* risk_rule_create_loss_limit(const char* name, double max_loss_ratio,
                                         double max_loss_amount);

/* 交易次数限制规则 */
risk_rule_t* risk_rule_create_trade_limit(const char* name, int max_trades_per_day);

/* 黑名单规则 */
risk_rule_t* risk_rule_create_blacklist(const char* name, const char** symbols, int count);

/* 白名单规则 */
risk_rule_t* risk_rule_create_whitelist(const char* name, const char** symbols, int count);

/* 价格偏离规则 */
risk_rule_t* risk_rule_create_price_deviation(const char* name, double max_deviation_ratio);

/* 成交量限制规则 */
risk_rule_t* risk_rule_create_volume_limit(const char* name, double max_volume_ratio);

/* 销毁风控规则 */
void risk_rule_destroy(risk_rule_t* rule);

/* ============ 监控系统 ============ */

/* 监控指标类型 */
typedef enum {
    METRIC_TYPE_COUNTER = 1,    /* 计数器 */
    METRIC_TYPE_GAUGE = 2,     /* 仪表 */
    METRIC_TYPE_HISTOGRAM = 3, /* 直方图 */
    METRIC_TYPE_SUMMARY = 4,   /* 摘要 */
} metric_type_t;

/* 监控指标 */
typedef struct {
    char name[STRING_MAX_LEN];
    metric_type_t type;
    char description[STRING_MAX_LEN];
    double value;
    timestamp_t timestamp;
    char labels[10][STRING_MAX_LEN];
    int label_count;
} metric_t;

/* 告警级别 */
typedef enum {
    ALERT_LEVEL_INFO = 1,
    ALERT_LEVEL_WARNING = 2,
    ALERT_LEVEL_ERROR = 3,
    ALERT_LEVEL_CRITICAL = 4,
} alert_level_t;

/* 告警 */
typedef struct {
    char name[STRING_MAX_LEN];
    alert_level_t level;
    char message[STRING_MAX_LEN];
    timestamp_t timestamp;
    int acknowledged;
} alert_t;

/* 告警回调 */
typedef void (*alert_callback_t)(const alert_t* alert, void* user_data);

/* 监控系统 */
typedef struct {
    metric_t* metrics;
    int metric_count;
    int max_metrics;
    alert_t* alerts;
    int alert_count;
    int max_alerts;
    alert_callback_t alert_callback;
    void* callback_user_data;
    int is_running;
} monitor_system_t;

/* 创建监控系统 */
monitor_system_t* monitor_system_create(void);

/* 销毁监控系统 */
void monitor_system_destroy(monitor_system_t* monitor);

/* 注册指标 */
int monitor_system_register_metric(monitor_system_t* monitor, const char* name,
                                    metric_type_t type, const char* description);

/* 更新指标 */
int monitor_system_update_metric(monitor_system_t* monitor, const char* name, double value);

/* 增加指标（计数器） */
int monitor_system_increment_metric(monitor_system_t* monitor, const char* name, double delta);

/* 查询指标 */
int monitor_system_query_metric(monitor_system_t* monitor, const char* name, metric_t* metric);

/* 触发告警 */
int monitor_system_trigger_alert(monitor_system_t* monitor, const char* name,
                                 alert_level_t level, const char* message);

/* 确认告警 */
int monitor_system_acknowledge_alert(monitor_system_t* monitor, const char* name);

/* 设置告警回调 */
int monitor_system_set_alert_callback(monitor_system_t* monitor,
                                       alert_callback_t callback, void* user_data);

/* 启动监控系统 */
int monitor_system_start(monitor_system_t* monitor);

/* 停止监控系统 */
int monitor_system_stop(monitor_system_t* monitor);

/* ============ 事后分析 ============ */

/* 盈亏归因 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    double profit_loss;
    double profit_loss_ratio;
    int64_t trade_count;
    int64_t win_count;
    int64_t loss_count;
    double avg_profit;
    double avg_loss;
    double max_profit;
    double max_loss;
} pnl_attribution_t;

/* 交易分析 */
typedef struct {
    int64_t total_trades;
    int64_t win_trades;
    int64_t loss_trades;
    double win_rate;
    double avg_profit;
    double avg_loss;
    double profit_factor;
    double max_consecutive_wins;
    double max_consecutive_losses;
    double avg_holding_time;
    double max_profit_trade;
    double max_loss_trade;
} trade_analysis_t;

/* 滑点分析 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    double total_slippage;
    double avg_slippage;
    double max_slippage;
    double slippage_ratio;
    int64_t trade_count;
} slippage_analysis_t;

/* 分析器 */
typedef struct {
    trade_record_t* trades;
    int trade_count;
    order_response_t* orders;
    int order_count;
} analyzer_t;

/* 创建分析器 */
analyzer_t* analyzer_create(void);

/* 销毁分析器 */
void analyzer_destroy(analyzer_t* analyzer);

/* 添加交易记录 */
int analyzer_add_trade(analyzer_t* analyzer, const trade_record_t* trade);

/* 添加订单记录 */
int analyzer_add_order(analyzer_t* analyzer, const order_response_t* order);

/* 盈亏归因分析 */
int analyzer_analyze_pnl_attribution(analyzer_t* analyzer, pnl_attribution_t** results, int* count);

/* 交易分析 */
int analyzer_analyze_trades(analyzer_t* analyzer, trade_analysis_t* analysis);

/* 滑点分析 */
int analyzer_analyze_slippage(analyzer_t* analyzer, slippage_analysis_t** results, int* count);

/* 生成分析报告 */
int analyzer_generate_report(analyzer_t* analyzer, const char* filename);

#endif /* GNTS_RISK_RISK_MANAGER_H */