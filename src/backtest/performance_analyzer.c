/*
 * GNTS - 观澜 · Nexus Trading System
 * 绩效评估工具实现
 */

#include "backtest/backtest_engine.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 绩效评估器内部结构 */
struct performance_analyzer_impl {
    double* returns;
    int return_count;
    int max_returns;
    double risk_free_rate;
};

/* 创建绩效评估器 */
performance_analyzer_t* performance_analyzer_create(double risk_free_rate) {
    struct performance_analyzer_impl* analyzer = (struct performance_analyzer_impl*)malloc(sizeof(struct performance_analyzer_impl));
    if (analyzer == NULL) {
        LOG_ERROR("Failed to allocate memory for performance analyzer");
        return NULL;
    }

    analyzer->returns = NULL;
    analyzer->return_count = 0;
    analyzer->max_returns = 0;
    analyzer->risk_free_rate = risk_free_rate;

    LOG_INFO("Performance analyzer created with risk-free rate %.4f", risk_free_rate);
    return (performance_analyzer_t*)analyzer;
}

/* 销毁绩效评估器 */
void performance_analyzer_destroy(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->returns != NULL) {
        free(impl->returns);
    }

    free(impl);
    LOG_INFO("Performance analyzer destroyed");
}

/* 添加收益率 */
int performance_analyzer_add_return(performance_analyzer_t* analyzer, double ret) {
    if (analyzer == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->returns, impl->return_count, impl->max_returns,
                              sizeof(double), 1024);

    impl->returns[impl->return_count++] = ret;

    return GNTS_OK;
}

/* 计算夏普比率 */
double performance_analyzer_calculate_sharpe(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 1) {
        return 0.0;
    }

    /* 计算平均收益率 */
    double sum = 0.0;
    for (int i = 0; i < impl->return_count; i++) {
        sum += impl->returns[i];
    }
    double mean_return = sum / impl->return_count;

    /* 计算标准差 */
    double sum_sq = 0.0;
    for (int i = 0; i < impl->return_count; i++) {
        sum_sq += (impl->returns[i] - mean_return) * (impl->returns[i] - mean_return);
    }
    double std_return = sqrt(sum_sq / (impl->return_count - 1));

    if (std_return == 0.0) {
        return 0.0;
    }

    /* 夏普比率 = (平均收益率 - 无风险利率) / 标准差 */
    double sharpe = (mean_return - impl->risk_free_rate) / std_return;

    LOG_DEBUG("Sharpe ratio: %.4f", sharpe);
    return sharpe;
}

/* 计算索提诺比率 */
double performance_analyzer_calculate_sortino(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 1) {
        return 0.0;
    }

    /* 计算平均收益率 */
    double sum = 0.0;
    for (int i = 0; i < impl->return_count; i++) {
        sum += impl->returns[i];
    }
    double mean_return = sum / impl->return_count;

    /* 计算下行标准差（只考虑负收益） */
    double sum_sq_down = 0.0;
    int down_count = 0;

    for (int i = 0; i < impl->return_count; i++) {
        if (impl->returns[i] < impl->risk_free_rate) {
            double down_return = impl->returns[i] - impl->risk_free_rate;
            sum_sq_down += down_return * down_return;
            down_count++;
        }
    }

    if (down_count == 0) {
        return 0.0;
    }

    double down_std = sqrt(sum_sq_down / down_count);

    if (down_std == 0.0) {
        return 0.0;
    }

    /* 索提诺比率 = (平均收益率 - 无风险利率) / 下行标准差 */
    double sortino = (mean_return - impl->risk_free_rate) / down_std;

    LOG_DEBUG("Sortino ratio: %.4f", sortino);
    return sortino;
}

/* 计算最大回撤 */
double performance_analyzer_calculate_max_drawdown(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 1) {
        return 0.0;
    }

    /* 计算累积收益率 */
    double* cumulative = (double*)malloc(sizeof(double) * impl->return_count);
    if (cumulative == NULL) {
        LOG_ERROR("Failed to allocate memory for cumulative returns");
        return 0.0;
    }

    cumulative[0] = 1.0 + impl->returns[0];
    for (int i = 1; i < impl->return_count; i++) {
        cumulative[i] = cumulative[i-1] * (1.0 + impl->returns[i]);
    }

    /* 计算最大回撤 */
    double max_cumulative = cumulative[0];
    double max_drawdown = 0.0;

    for (int i = 1; i < impl->return_count; i++) {
        if (cumulative[i] > max_cumulative) {
            max_cumulative = cumulative[i];
        }

        double drawdown = (max_cumulative - cumulative[i]) / max_cumulative;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
    }

    free(cumulative);

    LOG_DEBUG("Max drawdown: %.4f", max_drawdown);
    return max_drawdown;
}

/* 计算年化收益率 */
double performance_analyzer_calculate_annual_return(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 0) {
        return 0.0;
    }

    /* 计算总收益率 */
    double total_return = 1.0;
    for (int i = 0; i < impl->return_count; i++) {
        total_return *= (1.0 + impl->returns[i]);
    }
    total_return -= 1.0;

    /* 年化收益率 = (总收益率 + 1)^(252/天数) - 1 */
    /* 假设每个收益率对应一天 */
    double annual_return = pow(total_return + 1.0, 252.0 / impl->return_count) - 1.0;

    LOG_DEBUG("Annual return: %.4f", annual_return);
    return annual_return;
}

/* 计算波动率 */
double performance_analyzer_calculate_volatility(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 1) {
        return 0.0;
    }

    /* 计算平均收益率 */
    double sum = 0.0;
    for (int i = 0; i < impl->return_count; i++) {
        sum += impl->returns[i];
    }
    double mean_return = sum / impl->return_count;

    /* 计算标准差 */
    double sum_sq = 0.0;
    for (int i = 0; i < impl->return_count; i++) {
        sum_sq += (impl->returns[i] - mean_return) * (impl->returns[i] - mean_return);
    }
    double std_return = sqrt(sum_sq / (impl->return_count - 1));

    /* 年化波动率 = 日标准差 * sqrt(252) */
    double volatility = std_return * sqrt(252.0);

    LOG_DEBUG("Volatility: %.4f", volatility);
    return volatility;
}

/* 计算胜率 */
double performance_analyzer_calculate_win_rate(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 0) {
        return 0.0;
    }

    /* 计算盈利次数 */
    int win_count = 0;
    for (int i = 0; i < impl->return_count; i++) {
        if (impl->returns[i] > 0) {
            win_count++;
        }
    }

    double win_rate = (double)win_count / impl->return_count;

    LOG_DEBUG("Win rate: %.4f", win_rate);
    return win_rate;
}

/* 计算总收益率（累积） */
double performance_analyzer_calculate_total_return(performance_analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return 0.0;
    }

    struct performance_analyzer_impl* impl = (struct performance_analyzer_impl*)analyzer;

    if (impl->return_count <= 0) {
        return 0.0;
    }

    /* 计算累积收益率 */
    double total_return = 1.0;
    for (int i = 0; i < impl->return_count; i++) {
        total_return *= (1.0 + impl->returns[i]);
    }
    total_return -= 1.0;

    LOG_DEBUG("Total return: %.4f", total_return);
    return total_return;
}

/* 生成完整统计 */
int performance_analyzer_generate_statistics(performance_analyzer_t* analyzer,
                                               backtest_statistics_t* stats) {
    if (analyzer == NULL || stats == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    stats->total_return = performance_analyzer_calculate_total_return(analyzer);
    stats->annual_return = performance_analyzer_calculate_annual_return(analyzer);
    stats->max_drawdown = performance_analyzer_calculate_max_drawdown(analyzer);
    stats->sharpe_ratio = performance_analyzer_calculate_sharpe(analyzer);
    stats->sortino_ratio = performance_analyzer_calculate_sortino(analyzer);
    stats->win_rate = performance_analyzer_calculate_win_rate(analyzer);

    LOG_INFO("Performance statistics generated");
    return GNTS_OK;
}