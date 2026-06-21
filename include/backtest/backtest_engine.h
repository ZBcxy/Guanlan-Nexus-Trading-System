/*
 * GNTS - 观澜 · Nexus Trading System
 * 回测引擎接口定义
 */

#ifndef GNTS_BACKTEST_BACKTEST_ENGINE_H
#define GNTS_BACKTEST_BACKTEST_ENGINE_H

#include "common/types.h"
#include "common/structures.h"
#include "strategy/strategy_engine.h"

/* ============ 回测配置 ============ */

/* 手续费类型 */
typedef enum {
    COMMISSION_TYPE_FIXED = 1,      /* 固定手续费 */
    COMMISSION_TYPE_RATIO = 2,      /* 按比例 */
    COMMISSION_TYPE_TIERED = 3,     /* 分段收费 */
} commission_type_t;

/* 手续费配置 */
typedef struct {
    commission_type_t type;
    double value;                   /* 固定金额或比例 */
    double min_commission;          /* 最低手续费 */
} commission_config_t;

/* 滑点模型 */
typedef enum {
    SLIPPAGE_TYPE_FIXED = 1,        /* 固定滑点 */
    SLIPPAGE_TYPE_RATIO = 2,        /* 按比例滑点 */
    SLIPPAGE_TYPE_PRICE_IMPACT = 3, /* 价格冲击模型 */
} slippage_type_t;

/* 滑点配置 */
typedef struct {
    slippage_type_t type;
    double value;
    double price_impact_coefficient; /* 价格冲击系数 */
} slippage_config_t;

/* 回测配置 */
typedef struct {
    timestamp_t start_time;
    timestamp_t end_time;
    double initial_capital;         /* 初始资金 */
    commission_config_t commission; /* 手续费配置 */
    slippage_config_t slippage;    /* 滑点配置 */
    double stamp_duty;              /* 印花税（股票） */
    double transfer_fee;            /* 过户费 */
    int enable_short;               /* 是否允许做空 */
    int enable_margin;              /* 是否允许融资融券 */
    double margin_ratio;            /* 保证金比例 */
} backtest_config_t;

/* ============ 回测账户 ============ */

/* 回测账户 */
typedef struct {
    double initial_capital;         /* 初始资金 */
    double cash;                    /* 现金 */
    double frozen_cash;             /* 冻结资金 */
    double market_value;            /* 市值 */
    double total_asset;            /* 总资产 */
    double total_profit_loss;       /* 总盈亏 */
    double realized_profit_loss;    /* 已实现盈亏 */
    double unrealized_profit_loss;  /* 未实现盈亏 */
    position_t* positions;          /* 持仓列表 */
    int position_count;             /* 持仓数量 */
    int max_positions;              /* 最大持仓数 */
} backtest_account_t;

/* ============ 回测上下文 ============ */

/* 回测上下文 */
typedef struct {
    backtest_config_t config;
    backtest_account_t account;
    bar_data_t* bars;
    int bar_count;
    int current_index;
    timestamp_t current_time;
    void* strategy;
    void* user_data;
} backtest_context_t;

/* ============ 回测引擎 ============ */

/* 回测引擎 */
typedef struct {
    backtest_config_t config;
    backtest_account_t account;
    strategy_interface_t* strategy;
    bar_data_t** data_bars;
    int* bar_counts;
    char** symbols;
    int symbol_count;
    int is_running;
    backtest_statistics_t statistics;
} backtest_engine_t;

/* 创建回测引擎 */
backtest_engine_t* backtest_engine_create(const backtest_config_t* config);

/* 销毁回测引擎 */
void backtest_engine_destroy(backtest_engine_t* engine);

/* 设置策略 */
int backtest_engine_set_strategy(backtest_engine_t* engine, strategy_interface_t* strategy);

/* 加载历史数据 */
int backtest_engine_load_data(backtest_engine_t* engine, const char* symbol,
                              const bar_data_t* bars, int count);

/* 运行回测 */
int backtest_engine_run(backtest_engine_t* engine);

/* 暂停回测 */
int backtest_engine_pause(backtest_engine_t* engine);

/* 恢复回测 */
int backtest_engine_resume(backtest_engine_t* engine);

/* 停止回测 */
int backtest_engine_stop(backtest_engine_t* engine);

/* 获取回测结果 */
int backtest_engine_get_statistics(backtest_engine_t* engine, backtest_statistics_t* stats);

/* 获取交易记录 */
int backtest_engine_get_trades(backtest_engine_t* engine, trade_record_t** trades, int* count);

/* 获取订单记录 */
int backtest_engine_get_orders(backtest_engine_t* engine, order_response_t** orders, int* count);

/* 获取资金曲线 */
int backtest_engine_get_equity_curve(backtest_engine_t* engine, double** curve, int* count);

/* ============ 绩效评估 ============ */

/* 绩效评估器 */
typedef struct {
    double* returns;                /* 收益率序列 */
    int return_count;
    double risk_free_rate;          /* 无风险利率 */
} performance_analyzer_t;

/* 创建绩效评估器 */
performance_analyzer_t* performance_analyzer_create(double risk_free_rate);

/* 销毁绩效评估器 */
void performance_analyzer_destroy(performance_analyzer_t* analyzer);

/* 添加收益率 */
int performance_analyzer_add_return(performance_analyzer_t* analyzer, double ret);

/* 计算夏普比率 */
double performance_analyzer_calculate_sharpe(performance_analyzer_t* analyzer);

/* 计算索提诺比率 */
double performance_analyzer_calculate_sortino(performance_analyzer_t* analyzer);

/* 计算最大回撤 */
double performance_analyzer_calculate_max_drawdown(performance_analyzer_t* analyzer);

/* 计算总收益率（累积） */
double performance_analyzer_calculate_total_return(performance_analyzer_t* analyzer);

/* 计算年化收益率 */
double performance_analyzer_calculate_annual_return(performance_analyzer_t* analyzer);

/* 计算波动率 */
double performance_analyzer_calculate_volatility(performance_analyzer_t* analyzer);

/* 计算胜率 */
double performance_analyzer_calculate_win_rate(performance_analyzer_t* analyzer);

/* 生成完整统计 */
int performance_analyzer_generate_statistics(performance_analyzer_t* analyzer,
                                               backtest_statistics_t* stats);

/* ============ 回测报告 ============ */

/* 回测报告 */
typedef struct {
    backtest_statistics_t statistics;
    double* equity_curve;
    int equity_count;
    trade_record_t* trades;
    int trade_count;
    order_response_t* orders;
    int order_count;
    char* report_html;
    char* report_json;
} backtest_report_t;

/* 创建回测报告 */
backtest_report_t* backtest_report_create(backtest_engine_t* engine);

/* 销毁回测报告 */
void backtest_report_destroy(backtest_report_t* report);

/* 生成HTML报告 */
int backtest_report_generate_html(backtest_report_t* report, const char* filename);

/* 生成JSON报告 */
int backtest_report_generate_json(backtest_report_t* report, const char* filename);

#endif /* GNTS_BACKTEST_BACKTEST_ENGINE_H */