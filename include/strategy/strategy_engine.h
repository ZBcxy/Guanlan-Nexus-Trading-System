/*
 * GNTS - 观澜 · Nexus Trading System
 * 策略研究平台接口定义
 */

#ifndef GNTS_STRATEGY_STRATEGY_ENGINE_H
#define GNTS_STRATEGY_STRATEGY_ENGINE_H

#include "common/types.h"
#include "common/structures.h"

/* ============ 因子定义 ============ */

/* 因子类型 */
typedef enum {
    FACTOR_TYPE_TECHNICAL = 1,   /* 技术因子 */
    FACTOR_TYPE_FUNDAMENTAL = 2, /* 基本面因子 */
    FACTOR_TYPE_SENTIMENT = 3,   /* 情绪因子 */
    FACTOR_TYPE_CUSTOM = 99,     /* 自定义因子 */
} factor_type_t;

/* 前向声明 */
typedef struct factor_t factor_t;

/* 因子计算函数 */
typedef double (*factor_calc_func_t)(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count);

/* 因子定义 */
struct factor_t {
    char name[STRING_MAX_LEN];        /* 因子名称 */
    char description[STRING_MAX_LEN]; /* 因子描述 */
    factor_type_t type;               /* 因子类型 */
    factor_calc_func_t calc_func;     /* 计算函数 */
    void* params;                     /* 参数 */
};

/* ============ 策略接口 ============ */

/* 策略状态 */
typedef enum {
    STRATEGY_STATUS_STOPPED = 0,
    STRATEGY_STATUS_RUNNING = 1,
    STRATEGY_STATUS_PAUSED = 2,
} strategy_status_t;

/* 策略信号 */
typedef enum {
    SIGNAL_NONE = 0,
    SIGNAL_BUY = 1,
    SIGNAL_SELL = 2,
    SIGNAL_HOLD = 3,
} signal_t;

/* 策略上下文 */
typedef struct {
    void* data_manager;
    void* order_manager;
    void* risk_manager;
    void* user_data;
} strategy_context_t;

/* 策略接口 */
typedef struct strategy_interface {
    char name[STRING_MAX_LEN];        /* 策略名称 */
    char id[64];                      /* 策略ID */
    strategy_status_t status;         /* 策略状态 */

    /* 策略生命周期 */
    int (*on_init)(struct strategy_interface* self, strategy_context_t* ctx);
    void (*on_start)(struct strategy_interface* self);
    void (*on_stop)(struct strategy_interface* self);
    void (*on_pause)(struct strategy_interface* self);
    void (*on_resume)(struct strategy_interface* self);

    /* 行情事件 */
    void (*on_tick)(struct strategy_interface* self, const tick_data_t* tick);
    void (*on_bar)(struct strategy_interface* self, const bar_data_t* bar);

    /* 订单事件 */
    void (*on_order)(struct strategy_interface* self, const order_response_t* order);
    void (*on_trade)(struct strategy_interface* self, const trade_record_t* trade);

    /* 策略逻辑 */
    signal_t (*generate_signal)(struct strategy_interface* self, const char* symbol);

    void* private_data;
} strategy_interface_t;

/* ============ 参数优化 ============ */

/* 参数类型 */
typedef enum {
    PARAM_TYPE_INT = 1,
    PARAM_TYPE_DOUBLE = 2,
    PARAM_TYPE_BOOL = 3,
    PARAM_TYPE_STRING = 4,
} param_type_t;

/* 参数范围 */
typedef struct {
    char name[STRING_MAX_LEN];
    param_type_t type;
    union {
        struct { int min; int max; int step; } int_range;
        struct { double min; double max; double step; } double_range;
        struct { int value; } bool_value;
        struct { char values[10][STRING_MAX_LEN]; int count; } string_values;
    } range;
} param_range_t;

/* 优化目标 */
typedef enum {
    OPTIMIZE_TARGET_RETURN = 1,       /* 收益率 */
    OPTIMIZE_TARGET_SHARPE = 2,       /* 夏普比率 */
    OPTIMIZE_TARGET_DRAWDOWN = 3,     /* 最大回撤 */
    OPTIMIZE_TARGET_WIN_RATE = 4,     /* 胜率 */
    OPTIMIZE_TARGET_CUSTOM = 99,      /* 自定义目标 */
} optimize_target_t;

/* 优化方法 */
typedef enum {
    OPTIMIZE_METHOD_GRID = 1,         /* 网格搜索 */
    OPTIMIZE_METHOD_GENETIC = 2,      /* 遗传算法 */
    OPTIMIZE_METHOD_RANDOM = 3,       /* 随机搜索 */
    OPTIMIZE_METHOD_BAYESIAN = 4,     /* 贝叶斯优化 */
} optimize_method_t;

/* 优化配置 */
typedef struct {
    param_range_t* params;
    int param_count;
    optimize_target_t target;
    optimize_method_t method;
    int max_iterations;
    int parallel_workers;
} optimize_config_t;

/* 优化结果 */
typedef struct {
    double best_value;
    void* best_params;
    int iteration_count;
    double* history_values;
    int history_count;
} optimize_result_t;

/* ============ 策略引擎 ============ */

/* 策略引擎 */
typedef struct {
    strategy_interface_t** strategies;
    int strategy_count;
    strategy_context_t context;
    int is_running;
} strategy_engine_t;

/* 创建策略引擎 */
strategy_engine_t* strategy_engine_create(void);

/* 销毁策略引擎 */
void strategy_engine_destroy(strategy_engine_t* engine);

/* 添加策略 */
int strategy_engine_add_strategy(strategy_engine_t* engine, strategy_interface_t* strategy);

/* 移除策略 */
int strategy_engine_remove_strategy(strategy_engine_t* engine, const char* strategy_id);

/* 启动策略 */
int strategy_engine_start_strategy(strategy_engine_t* engine, const char* strategy_id);

/* 停止策略 */
int strategy_engine_stop_strategy(strategy_engine_t* engine, const char* strategy_id);

/* 暂停策略 */
int strategy_engine_pause_strategy(strategy_engine_t* engine, const char* strategy_id);

/* 恢复策略 */
int strategy_engine_resume_strategy(strategy_engine_t* engine, const char* strategy_id);

/* 启动引擎 */
int strategy_engine_start(strategy_engine_t* engine);

/* 停止引擎 */
int strategy_engine_stop(strategy_engine_t* engine);

/* 设置数据管理器（集成数据系统） */
int strategy_engine_set_data_manager(strategy_engine_t* engine, void* data_manager);

/* 设置订单管理器（集成OMS） */
int strategy_engine_set_order_manager(strategy_engine_t* engine, void* order_manager);

/* 手动喂入K线数据（回测/离线模式） */
int strategy_engine_feed_bar(strategy_engine_t* engine, const bar_data_t* bar);

/* 手动喂入Tick数据 */
int strategy_engine_feed_tick(strategy_engine_t* engine, const tick_data_t* tick);

/* ============ 因子挖掘工具 ============ */

/* 因子参数（可配置） */
typedef struct {
    int period;              /* 周期 */
    int fast_period;         /* 快线周期（MACD） */
    int slow_period;         /* 慢线周期（MACD） */
    int signal_period;       /* 信号线周期（MACD） */
    double multiplier;       /* 标准差倍数（BOLL） */
} factor_params_t;

/* 创建因子 */
factor_t* factor_create(const char* name, factor_type_t type, factor_calc_func_t calc_func);

/* 销毁因子 */
void factor_destroy(factor_t* factor);

/* 计算因子值 */
double factor_calculate(factor_t* factor, const char* symbol, const bar_data_t* bars, int count);

/* 设置因子参数 */
int factor_set_params(factor_t* factor, const factor_params_t* params);

/* 预定义技术因子构造函数（默认参数） */
factor_t* create_factor_ma(void);
factor_t* create_factor_ema(void);
factor_t* create_factor_macd(void);
factor_t* create_factor_rsi(void);
factor_t* create_factor_boll_upper(void);
factor_t* create_factor_boll_lower(void);
factor_t* create_factor_atr(void);

/* 预定义技术因子构造函数（可配置参数） */
factor_t* create_factor_ma_with_params(int period);
factor_t* create_factor_ema_with_params(int period);
factor_t* create_factor_macd_with_params(int fast, int slow, int signal);
factor_t* create_factor_rsi_with_params(int period);
factor_t* create_factor_boll_with_params(int period, double multiplier);
factor_t* create_factor_atr_with_params(int period);

/* ============ 参数优化工具 ============ */

/* 创建优化器 */
void* optimizer_create(const optimize_config_t* config);

/* 销毁优化器 */
void optimizer_destroy(void* optimizer);

/* 运行优化 */
int optimizer_run(void* optimizer, strategy_interface_t* strategy,
                  const bar_data_t* bars, int bar_count,
                  optimize_result_t* result);

/* 获取最优参数 */
int optimizer_get_best_params(void* optimizer, void* params);

#endif /* GNTS_STRATEGY_STRATEGY_ENGINE_H */