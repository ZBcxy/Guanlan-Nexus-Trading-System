/*
 * GNTS - 观澜 · Nexus Trading System
 * 参数优化工具实现
 */

#include "strategy/strategy_engine.h"
#include "backtest/backtest_engine.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* 优化器内部结构 */
typedef struct {
    optimize_config_t config;
    double* values;
    int value_count;
    int current_iteration;
    double* best_params_values;     /* 最优参数值数组 */
    int total_combinations;         /* 网格搜索总组合数 */
} optimizer_impl_t;

/* 计算单个参数的总步数 */
static int param_step_count(const param_range_t* param) {
    switch (param->type) {
        case PARAM_TYPE_INT:
            if (param->range.int_range.step <= 0) return 1;
            return (param->range.int_range.max - param->range.int_range.min) /
                   param->range.int_range.step + 1;
        case PARAM_TYPE_DOUBLE:
            if (param->range.double_range.step <= 0.0) return 1;
            return (int)((param->range.double_range.max - param->range.double_range.min) /
                   param->range.double_range.step) + 1;
        case PARAM_TYPE_BOOL:
            return 2;
        case PARAM_TYPE_STRING:
            return param->range.string_values.count > 0 ?
                   param->range.string_values.count : 1;
        default:
            return 1;
    }
}

/* 计算网格搜索总组合数 */
static int compute_total_combinations(const optimize_config_t* config) {
    int total = 1;
    for (int i = 0; i < config->param_count; i++) {
        total *= param_step_count(&config->params[i]);
        if (total > 1000000) {
            LOG_WARNING("Grid search combinations exceed 1,000,000, capping at max_iterations");
            return config->max_iterations;
        }
    }
    return total;
}

/* 根据迭代索引生成参数值 */
static void generate_grid_params(optimizer_impl_t* optimizer, int iteration, double* param_values) {
    int remaining = iteration;
    for (int i = optimizer->config.param_count - 1; i >= 0; i--) {
        const param_range_t* param = &optimizer->config.params[i];
        int steps = param_step_count(param);

        int idx = remaining % steps;
        remaining /= steps;

        switch (param->type) {
            case PARAM_TYPE_INT:
                param_values[i] = (double)(param->range.int_range.min +
                                   idx * param->range.int_range.step);
                break;
            case PARAM_TYPE_DOUBLE:
                param_values[i] = param->range.double_range.min +
                                  idx * param->range.double_range.step;
                break;
            case PARAM_TYPE_BOOL:
                param_values[i] = (double)idx;
                break;
            case PARAM_TYPE_STRING:
                param_values[i] = (double)idx; /* 字符串用索引表示 */
                break;
        }
    }
}

/* 生成随机参数 */
static void generate_random_params(optimizer_impl_t* optimizer, double* param_values) {
    for (int i = 0; i < optimizer->config.param_count; i++) {
        const param_range_t* param = &optimizer->config.params[i];
        switch (param->type) {
            case PARAM_TYPE_INT: {
                int range = param->range.int_range.max - param->range.int_range.min;
                int steps = range / param->range.int_range.step;
                param_values[i] = (double)(param->range.int_range.min +
                                   (rand() % (steps + 1)) * param->range.int_range.step);
                break;
            }
            case PARAM_TYPE_DOUBLE: {
                double range = param->range.double_range.max - param->range.double_range.min;
                double r = (double)rand() / RAND_MAX;
                param_values[i] = param->range.double_range.min + r * range;
                break;
            }
            case PARAM_TYPE_BOOL:
                param_values[i] = (double)(rand() % 2);
                break;
            case PARAM_TYPE_STRING:
                param_values[i] = (double)(rand() % param->range.string_values.count);
                break;
        }
    }
}

/* 设置策略参数（通过策略的 private_data 传递） */
static void apply_params_to_strategy(strategy_interface_t* strategy,
                                     const optimize_config_t* config,
                                     const double* param_values) {
    if (strategy->private_data == NULL) {
        return;
    }

    /* 将参数值写入策略的 private_data 起始位置 */
    /* 约定：策略的 private_data 前 param_count * sizeof(double) 字节存放参数值 */
    double* strategy_params = (double*)strategy->private_data;
    for (int i = 0; i < config->param_count; i++) {
        strategy_params[i] = param_values[i];
    }
}

/* 计算目标函数值：使用简化前向测试 */
static double calculate_objective(strategy_interface_t* strategy,
                                  const bar_data_t* bars, int bar_count,
                                  const optimize_config_t* config,
                                  const double* param_values) {
    if (bar_count < 2) {
        return -DBL_MAX;
    }

    /* 应用参数到策略 */
    apply_params_to_strategy(strategy, config, param_values);

    /* 简化前向测试：逐根K线调用策略信号，模拟交易 */
    double capital = 1000000.0;       /* 模拟初始资金 */
    double cash = capital;
    double position = 0.0;            /* 持仓数量 */
    double position_value = 0.0;      /* 持仓市值 */
    double equity_peak = capital;     /* 资金峰值 */
    double max_drawdown = 0.0;
    int total_trades = 0;
    int win_trades = 0;
    double total_return = 0.0;
    double last_trade_return = 0.0;

    /* 收益率序列（用于计算夏普比率） */
    double* period_returns = (double*)malloc(sizeof(double) * (bar_count - 1));
    int return_idx = 0;

    double entry_price = 0.0;

    for (int i = 0; i < bar_count; i++) {
        signal_t signal = strategy->generate_signal(strategy, bars[i].symbol);

        double current_price = bars[i].close;
        double equity = cash + position * current_price;

        /* 记录资金曲线 */
        if (equity > equity_peak) {
            equity_peak = equity;
        }
        double dd = (equity_peak - equity) / equity_peak;
        if (dd > max_drawdown) {
            max_drawdown = dd;
        }

        /* 记录期间收益率 */
        if (i > 0 && return_idx < bar_count - 1) {
            double prev_equity = cash + position * bars[i-1].close;
            if (prev_equity > 0) {
                period_returns[return_idx++] = (equity - prev_equity) / prev_equity;
            }
        }

        /* 执行交易信号 */
        if (signal == SIGNAL_BUY && position == 0.0) {
            /* 开仓：全仓买入 */
            double commission = cash * 0.0003; /* 0.03% 手续费 */
            double available = cash - commission;
            if (available > 0 && current_price > 0) {
                position = available / current_price;
                cash -= available + commission;
                entry_price = current_price;
                position_value = position * current_price;
            }
        } else if (signal == SIGNAL_SELL && position > 0.0) {
            /* 平仓 */
            double sell_value = position * current_price;
            double commission = sell_value * 0.0003;
            double stamp_duty = sell_value * 0.001; /* 印花税 */
            cash += sell_value - commission - stamp_duty;
            position_value = 0.0;

            /* 统计交易 */
            total_trades++;
            double trade_return = (sell_value - commission - stamp_duty -
                                   position * entry_price) / (position * entry_price);
            if (trade_return > 0) {
                win_trades++;
            }
            last_trade_return = trade_return;
            position = 0.0;
            entry_price = 0.0;
        }
    }

    /* 如果还有持仓，按最后价格平仓 */
    if (position > 0.0) {
        double final_price = bars[bar_count - 1].close;
        cash += position * final_price;
        total_trades++;
        double trade_return = (position * final_price - position * entry_price) /
                              (position * entry_price);
        if (trade_return > 0) {
            win_trades++;
        }
        position = 0.0;
    }

    /* 总收益率 */
    total_return = (cash - capital) / capital;

    /* 计算夏普比率 */
    double sharpe = 0.0;
    if (return_idx > 0) {
        double mean_ret = 0.0;
        for (int i = 0; i < return_idx; i++) {
            mean_ret += period_returns[i];
        }
        mean_ret /= return_idx;

        double var = 0.0;
        for (int i = 0; i < return_idx; i++) {
            double diff = period_returns[i] - mean_ret;
            var += diff * diff;
        }
        var /= return_idx;

        if (var > 0.0) {
            sharpe = mean_ret / sqrt(var) * sqrt(252.0); /* 年化 */
        }
    }

    double win_rate = total_trades > 0 ? (double)win_trades / total_trades : 0.0;

    free(period_returns);

    /* 根据优化目标返回分数 */
    switch (config->target) {
        case OPTIMIZE_TARGET_RETURN:
            return total_return;
        case OPTIMIZE_TARGET_SHARPE:
            return sharpe;
        case OPTIMIZE_TARGET_DRAWDOWN:
            return -max_drawdown; /* 越大越好，所以取负值 */
        case OPTIMIZE_TARGET_WIN_RATE:
            return win_rate;
        case OPTIMIZE_TARGET_CUSTOM:
            /* 综合评分：收益率 + 夏普 - 回撤 */
            return total_return * 0.4 + sharpe * 0.3 - max_drawdown * 0.3;
        default:
            return total_return;
    }
}

/* 创建优化器 */
void* optimizer_create(const optimize_config_t* config) {
    if (config == NULL) {
        LOG_ERROR("Invalid config for optimizer");
        return NULL;
    }

    if (config->params == NULL || config->param_count <= 0) {
        LOG_ERROR("No parameters specified for optimization");
        return NULL;
    }

    optimizer_impl_t* optimizer = (optimizer_impl_t*)malloc(sizeof(optimizer_impl_t));
    if (optimizer == NULL) {
        LOG_ERROR("Failed to allocate memory for optimizer");
        return NULL;
    }

    optimizer->config = *config;

    /* 复制参数配置 */
    optimizer->config.params = (param_range_t*)malloc(
        sizeof(param_range_t) * config->param_count);
    if (optimizer->config.params == NULL) {
        LOG_ERROR("Failed to allocate params copy");
        free(optimizer);
        return NULL;
    }
    memcpy(optimizer->config.params, config->params,
           sizeof(param_range_t) * config->param_count);

    /* 计算网格搜索总组合数 */
    optimizer->total_combinations = compute_total_combinations(config);

    int alloc_size = optimizer->total_combinations > 0 ?
                     optimizer->total_combinations : config->max_iterations;

    optimizer->values = (double*)malloc(sizeof(double) * alloc_size);
    if (optimizer->values == NULL) {
        LOG_ERROR("Failed to allocate memory for values");
        free(optimizer->config.params);
        free(optimizer);
        return NULL;
    }

    optimizer->best_params_values = (double*)malloc(
        sizeof(double) * config->param_count);
    if (optimizer->best_params_values == NULL) {
        LOG_ERROR("Failed to allocate best params");
        free(optimizer->values);
        free(optimizer->config.params);
        free(optimizer);
        return NULL;
    }

    optimizer->value_count = 0;
    optimizer->current_iteration = 0;

    LOG_INFO("Optimizer created: method=%d, params=%d, combinations=%d",
             config->method, config->param_count, optimizer->total_combinations);
    return optimizer;
}

/* 销毁优化器 */
void optimizer_destroy(void* optimizer) {
    if (optimizer == NULL) {
        return;
    }

    optimizer_impl_t* impl = (optimizer_impl_t*)optimizer;

    if (impl->values != NULL) {
        free(impl->values);
    }
    if (impl->best_params_values != NULL) {
        free(impl->best_params_values);
    }
    if (impl->config.params != NULL) {
        free(impl->config.params);
    }

    free(impl);
    LOG_INFO("Optimizer destroyed");
}

/* 运行优化 */
int optimizer_run(void* optimizer, strategy_interface_t* strategy,
                  const bar_data_t* bars, int bar_count,
                  optimize_result_t* result) {
    if (optimizer == NULL || strategy == NULL || bars == NULL || result == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    optimizer_impl_t* impl = (optimizer_impl_t*)optimizer;

    int iterations;
    if (impl->config.method == OPTIMIZE_METHOD_GRID) {
        iterations = impl->total_combinations;
        if (iterations > impl->config.max_iterations) {
            iterations = impl->config.max_iterations;
        }
    } else {
        iterations = impl->config.max_iterations;
    }

    LOG_INFO("Starting optimization: method=%d, iterations=%d, bars=%d",
             impl->config.method, iterations, bar_count);

    double best_value = -DBL_MAX;
    double* current_params = (double*)malloc(
        sizeof(double) * impl->config.param_count);
    if (current_params == NULL) {
        LOG_ERROR("Failed to allocate current params");
        return GNTS_ERROR_MEMORY;
    }

    for (int i = 0; i < iterations; i++) {
        /* 生成参数组合 */
        switch (impl->config.method) {
            case OPTIMIZE_METHOD_GRID:
                generate_grid_params(impl, i, current_params);
                break;
            case OPTIMIZE_METHOD_RANDOM:
                generate_random_params(impl, current_params);
                break;
            case OPTIMIZE_METHOD_GENETIC:
                /* 遗传算法：暂未实现，使用随机搜索替代 */
                generate_random_params(impl, current_params);
                break;
            case OPTIMIZE_METHOD_BAYESIAN:
                /* 贝叶斯优化：暂未实现，使用随机搜索替代 */
                generate_random_params(impl, current_params);
                break;
            default:
                LOG_ERROR("Unknown optimization method: %d", impl->config.method);
                free(current_params);
                return GNTS_ERROR_INVALID_PARAM;
        }

        /* 计算目标值 */
        double value = calculate_objective(strategy, bars, bar_count,
                                           &impl->config, current_params);

        /* 保存结果 */
        impl->values[impl->value_count++] = value;

        /* 更新最优值 */
        if (value > best_value) {
            best_value = value;
            memcpy(impl->best_params_values, current_params,
                   sizeof(double) * impl->config.param_count);
        }

        if (i % 100 == 0 || i == iterations - 1) {
            LOG_DEBUG("Iteration %d/%d: value=%.4f, best=%.4f",
                      i + 1, iterations, value, best_value);
        }
    }

    /* 构建结果 */
    result->best_value = best_value;

    /* 复制最优参数 */
    result->best_params = malloc(sizeof(double) * impl->config.param_count);
    if (result->best_params != NULL) {
        memcpy(result->best_params, impl->best_params_values,
               sizeof(double) * impl->config.param_count);
    }

    result->iteration_count = impl->value_count;
    result->history_values = impl->values;
    result->history_count = impl->value_count;

    free(current_params);

    LOG_INFO("Optimization completed: best_value=%.4f, total_iterations=%d",
             best_value, impl->value_count);
    return GNTS_OK;
}

/* 获取最优参数 */
int optimizer_get_best_params(void* optimizer, void* params) {
    if (optimizer == NULL || params == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    optimizer_impl_t* impl = (optimizer_impl_t*)optimizer;

    if (impl->best_params_values == NULL) {
        LOG_ERROR("No best params available. Run optimizer first.");
        return GNTS_ERROR;
    }

    memcpy(params, impl->best_params_values,
           sizeof(double) * impl->config.param_count);

    LOG_INFO("Best params retrieved");
    return GNTS_OK;
}