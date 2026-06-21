/*
 * GNTS - 观澜 · Nexus Trading System
 * 因子挖掘工具实现
 */

#include "strategy/strategy_engine.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 创建因子 */
factor_t* factor_create(const char* name, factor_type_t type, factor_calc_func_t calc_func) {
    if (name == NULL || calc_func == NULL) {
        LOG_ERROR("Invalid parameters for factor creation");
        return NULL;
    }

    factor_t* factor = (factor_t*)malloc(sizeof(factor_t));
    if (factor == NULL) {
        LOG_ERROR("Failed to allocate memory for factor");
        return NULL;
    }

    strncpy(factor->name, name, STRING_MAX_LEN - 1);
    factor->name[STRING_MAX_LEN - 1] = '\0';
    factor->type = type;
    factor->calc_func = calc_func;
    factor->params = NULL;
    factor->description[0] = '\0';

    LOG_INFO("Factor %s created", name);
    return factor;
}

/* 销毁因子 */
void factor_destroy(factor_t* factor) {
    if (factor == NULL) {
        return;
    }

    if (factor->params != NULL) {
        free(factor->params);
    }

    free(factor);
    LOG_INFO("Factor destroyed");
}

/* 计算因子值 */
double factor_calculate(factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    if (factor == NULL || symbol == NULL || bars == NULL || count <= 0) {
        LOG_ERROR("Invalid parameters for factor calculation");
        return 0.0;
    }

    if (factor->calc_func == NULL) {
        LOG_ERROR("Factor %s has no calculation function", factor->name);
        return 0.0;
    }

    double value = factor->calc_func(factor, symbol, bars, count);
    LOG_DEBUG("Factor %s value: %.4f for %s", factor->name, value, symbol);
    return value;
}

/* 设置因子参数 */
int factor_set_params(factor_t* factor, const factor_params_t* params) {
    if (factor == NULL || params == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (factor->params != NULL) {
        free(factor->params);
    }

    factor->params = malloc(sizeof(factor_params_t));
    if (factor->params == NULL) {
        LOG_ERROR("Failed to allocate factor params");
        return GNTS_ERROR_MEMORY;
    }

    memcpy(factor->params, params, sizeof(factor_params_t));
    LOG_INFO("Factor %s params updated: period=%d", factor->name, params->period);
    return GNTS_OK;
}

/* ============ 预定义技术因子 ============ */

/* 从因子参数中获取周期，未设置则使用默认值 */
static int get_period(const factor_t* factor, int default_period) {
    if (factor->params != NULL) {
        return ((const factor_params_t*)factor->params)->period;
    }
    return default_period;
}

/* MA因子 - 移动平均 */
static double factor_ma(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = get_period(factor, 5);
    (void)symbol;

    if (count < period) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = count - period; i < count; i++) {
        sum += bars[i].close;
    }

    return sum / period;
}

/* EMA因子 - 指数移动平均 */
static double factor_ema(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = get_period(factor, 10);
    (void)symbol;

    if (count < period) {
        return 0.0;
    }

    double alpha = 2.0 / (period + 1.0);
    double ema = bars[count - period].close;

    for (int i = count - period + 1; i < count; i++) {
        ema = alpha * bars[i].close + (1.0 - alpha) * ema;
    }

    return ema;
}

/* MACD因子 */
static double factor_macd(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int fast = 12, slow = 26;
    if (factor->params != NULL) {
        const factor_params_t* p = (const factor_params_t*)factor->params;
        fast = p->fast_period;
        slow = p->slow_period;
    }
    (void)symbol;

    if (count < slow) {
        return 0.0;
    }

    double alpha_fast = 2.0 / (fast + 1.0);
    double alpha_slow = 2.0 / (slow + 1.0);

    double ema_fast = bars[count - slow].close;
    double ema_slow = bars[count - slow].close;

    for (int i = count - slow + 1; i < count; i++) {
        ema_fast = alpha_fast * bars[i].close + (1.0 - alpha_fast) * ema_fast;
        ema_slow = alpha_slow * bars[i].close + (1.0 - alpha_slow) * ema_slow;
    }

    return ema_fast - ema_slow;
}

/* RSI因子 - 相对强弱指标 */
static double factor_rsi(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = get_period(factor, 14);
    (void)symbol;

    if (count < period + 1) {
        return 0.0;
    }

    double sum_up = 0.0;
    double sum_down = 0.0;

    for (int i = count - period; i < count; i++) {
        double change = bars[i].close - bars[i-1].close;
        if (change > 0) {
            sum_up += change;
        } else {
            sum_down += fabs(change);
        }
    }

    if (sum_down == 0.0) {
        return 100.0;
    }

    double rs = sum_up / sum_down;
    return 100.0 - (100.0 / (1.0 + rs));
}

/* BOLL因子 - 布林带上轨 */
static double factor_boll_upper(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = 20;
    double multiplier = 2.0;
    if (factor->params != NULL) {
        const factor_params_t* p = (const factor_params_t*)factor->params;
        period = p->period;
        multiplier = p->multiplier;
    }
    (void)symbol;

    if (count < period) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = count - period; i < count; i++) {
        sum += bars[i].close;
    }
    double ma = sum / period;

    double sum_sq = 0.0;
    for (int i = count - period; i < count; i++) {
        sum_sq += (bars[i].close - ma) * (bars[i].close - ma);
    }
    double std = sqrt(sum_sq / period);

    return ma + multiplier * std;
}

/* BOLL因子 - 布林带下轨 */
static double factor_boll_lower(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = 20;
    double multiplier = 2.0;
    if (factor->params != NULL) {
        const factor_params_t* p = (const factor_params_t*)factor->params;
        period = p->period;
        multiplier = p->multiplier;
    }
    (void)symbol;

    if (count < period) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = count - period; i < count; i++) {
        sum += bars[i].close;
    }
    double ma = sum / period;

    double sum_sq = 0.0;
    for (int i = count - period; i < count; i++) {
        sum_sq += (bars[i].close - ma) * (bars[i].close - ma);
    }
    double std = sqrt(sum_sq / period);

    return ma - multiplier * std;
}

/* ATR因子 - 平均真实波幅 */
static double factor_atr(const factor_t* factor, const char* symbol, const bar_data_t* bars, int count) {
    int period = get_period(factor, 14);
    (void)symbol;

    if (count < period + 1) {
        return 0.0;
    }

    double sum_tr = 0.0;

    for (int i = count - period; i < count; i++) {
        double tr1 = bars[i].high - bars[i].low;
        double tr2 = fabs(bars[i].high - bars[i-1].close);
        double tr3 = fabs(bars[i].low - bars[i-1].close);
        double tr = tr1 > tr2 ? tr1 : tr2;
        tr = tr > tr3 ? tr : tr3;
        sum_tr += tr;
    }

    return sum_tr / period;
}

/* ============ 预定义因子构造函数（默认参数） ============ */

factor_t* create_factor_ma(void) {
    return factor_create("MA5", FACTOR_TYPE_TECHNICAL, factor_ma);
}

factor_t* create_factor_ema(void) {
    return factor_create("EMA10", FACTOR_TYPE_TECHNICAL, factor_ema);
}

factor_t* create_factor_macd(void) {
    return factor_create("MACD", FACTOR_TYPE_TECHNICAL, factor_macd);
}

factor_t* create_factor_rsi(void) {
    return factor_create("RSI14", FACTOR_TYPE_TECHNICAL, factor_rsi);
}

factor_t* create_factor_boll_upper(void) {
    return factor_create("BOLL_UPPER", FACTOR_TYPE_TECHNICAL, factor_boll_upper);
}

factor_t* create_factor_boll_lower(void) {
    return factor_create("BOLL_LOWER", FACTOR_TYPE_TECHNICAL, factor_boll_lower);
}

factor_t* create_factor_atr(void) {
    return factor_create("ATR14", FACTOR_TYPE_TECHNICAL, factor_atr);
}

/* ============ 预定义因子构造函数（可配置参数） ============ */

factor_t* create_factor_ma_with_params(int period) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "MA%d", period);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_ma);
    if (factor != NULL) {
        factor_params_t params = { .period = period };
        factor_set_params(factor, &params);
    }
    return factor;
}

factor_t* create_factor_ema_with_params(int period) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "EMA%d", period);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_ema);
    if (factor != NULL) {
        factor_params_t params = { .period = period };
        factor_set_params(factor, &params);
    }
    return factor;
}

factor_t* create_factor_macd_with_params(int fast, int slow, int signal) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "MACD(%d,%d,%d)", fast, slow, signal);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_macd);
    if (factor != NULL) {
        factor_params_t params = {
            .fast_period = fast,
            .slow_period = slow,
            .signal_period = signal,
        };
        factor_set_params(factor, &params);
    }
    return factor;
}

factor_t* create_factor_rsi_with_params(int period) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "RSI%d", period);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_rsi);
    if (factor != NULL) {
        factor_params_t params = { .period = period };
        factor_set_params(factor, &params);
    }
    return factor;
}

factor_t* create_factor_boll_with_params(int period, double multiplier) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "BOLL(%d,%.1f)", period, multiplier);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_boll_upper);
    if (factor != NULL) {
        factor_params_t params = {
            .period = period,
            .multiplier = multiplier,
        };
        factor_set_params(factor, &params);
    }
    return factor;
}

factor_t* create_factor_atr_with_params(int period) {
    char name[STRING_MAX_LEN];
    snprintf(name, sizeof(name), "ATR%d", period);

    factor_t* factor = factor_create(name, FACTOR_TYPE_TECHNICAL, factor_atr);
    if (factor != NULL) {
        factor_params_t params = { .period = period };
        factor_set_params(factor, &params);
    }
    return factor;
}