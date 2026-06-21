/*
 * GNTS - 观澜 · Nexus Trading System
 * 数据结构定义
 */

#ifndef GNTS_COMMON_STRUCTURES_H
#define GNTS_COMMON_STRUCTURES_H

#include "types.h"

/* ============ 行情数据结构 ============ */

/* Tick数据 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    timestamp_t timestamp;            /* 时间戳 */
    double price;                    /* 最新价 */
    int64_t volume;                  /* 成交量 */
    double turnover;                /* 成交额 */
    double bid_price[5];             /* 买价1-5 */
    int64_t bid_volume[5];           /* 买量1-5 */
    double ask_price[5];             /* 卖价1-5 */
    int64_t ask_volume[5];           /* 卖量1-5 */
    double open;                     /* 开盘价 */
    double high;                     /* 最高价 */
    double low;                      /* 最低价 */
    double pre_close;                /* 昨收价 */
} tick_data_t;

/* K线数据 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    timestamp_t timestamp;            /* 时间戳 */
    bar_period_t period;             /* 周期 */
    double open;                     /* 开盘价 */
    double high;                     /* 最高价 */
    double low;                      /* 最低价 */
    double close;                    /* 收盘价 */
    int64_t volume;                  /* 成交量 */
    double turnover;                 /* 成交额 */
    int open_interest;               /* 持仓量（期货） */
} bar_data_t;

/* ============ 订单数据结构 ============ */

/* 订单请求 */
typedef struct {
    char order_id[64];               /* 订单ID */
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    order_direction_t direction;     /* 买卖方向 */
    order_type_t type;               /* 订单类型 */
    double price;                    /* 价格 */
    int64_t quantity;                /* 数量 */
    timestamp_t create_time;          /* 创建时间 */
    char strategy_id[64];            /* 策略ID */
} order_request_t;

/* 订单回报 */
typedef struct {
    char order_id[64];               /* 订单ID */
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    order_direction_t direction;     /* 买卖方向 */
    order_type_t type;               /* 订单类型 */
    double price;                    /* 价格 */
    int64_t quantity;                /* 委托数量 */
    int64_t filled_quantity;         /* 成交数量 */
    double filled_price;             /* 成交均价 */
    order_status_t status;           /* 订单状态 */
    timestamp_t create_time;          /* 创建时间 */
    timestamp_t update_time;         /* 更新时间 */
    char strategy_id[64];            /* 策略ID */
    char error_msg[STRING_MAX_LEN];  /* 错误信息 */
} order_response_t;

/* 成交记录 */
typedef struct {
    char trade_id[64];               /* 成交ID */
    char order_id[64];               /* 订单ID */
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    order_direction_t direction;     /* 买卖方向 */
    double price;                    /* 成交价格 */
    int64_t quantity;                /* 成交数量 */
    timestamp_t trade_time;          /* 成交时间 */
    char strategy_id[64];            /* 策略ID */
} trade_record_t;

/* ============ 持仓数据结构 ============ */

/* 持仓信息 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    int64_t position;                /* 持仓数量 */
    int64_t available;               /* 可用数量 */
    double cost_price;               /* 成本价 */
    double market_value;             /* 市值 */
    double profit_loss;              /* 盈亏 */
    double profit_loss_ratio;        /* 盈亏比例 */
    timestamp_t update_time;          /* 更新时间 */
} position_t;

/* 账户信息 */
typedef struct {
    double total_asset;              /* 总资产 */
    double cash;                     /* 现金 */
    double market_value;             /* 市值 */
    double profit_loss;              /* 总盈亏 */
    double available_cash;           /* 可用资金 */
    double margin;                   /* 保证金（期货） */
    timestamp_t update_time;          /* 更新时间 */
} account_info_t;

/* ============ 财务数据结构 ============ */

/* 财务数据 */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];     /* 股票代码 */
    timestamp_t report_date;          /* 报告日期 */
    double pe_ratio;                 /* 市盈率 */
    double pb_ratio;                 /* 市净率 */
    double roe;                      /* 净资产收益率 */
    double debt_ratio;               /* 资产负债率 */
    double current_ratio;            /* 流动比率 */
    double quick_ratio;              /* 速动比率 */
    double gross_margin;             /* 毛利率 */
    double net_margin;               /* 净利率 */
    double dividend_yield;           /* 股息率 */
    int64_t total_shares;            /* 总股本 */
    int64_t float_shares;            /* 流通股本 */
} financial_data_t;

/* ============ 回测结果结构 ============ */

/* 回测统计 */
typedef struct {
    double total_return;             /* 总收益率 */
    double annual_return;            /* 年化收益率 */
    double max_drawdown;             /* 最大回撤 */
    double sharpe_ratio;             /* 夏普比率 */
    double sortino_ratio;            /* 索提诺比率 */
    double win_rate;                 /* 胜率 */
    int64_t total_trades;            /* 总交易次数 */
    int64_t win_trades;              /* 盈利交易次数 */
    int64_t loss_trades;             /* 亏损交易次数 */
    double avg_profit;               /* 平均盈利 */
    double avg_loss;                 /* 平均亏损 */
    double profit_factor;            /* 盈亏比 */
    double max_consecutive_wins;      /* 最大连续盈利次数 */
    double max_consecutive_losses;    /* 最大连续亏损次数 */
} backtest_statistics_t;

#endif /* GNTS_COMMON_STRUCTURES_H */