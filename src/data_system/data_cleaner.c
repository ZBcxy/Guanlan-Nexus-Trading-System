/*
 * GNTS - 观澜 · Nexus Trading System
 * 数据清洗工具实现
 */

#include "data_system/data_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 除权除息事件最大数量 */
#define MAX_DIVIDEND_EVENTS 256

/* 数据清洗器内部结构 */
struct data_cleaner {
    dividend_event_t* dividend_events;
    int dividend_count;
    int dividend_capacity;
};

/* 计算除权除息调整因子 */
static double calc_dividend_factor(const dividend_event_t* event, double close_price) {
    if (close_price <= 0.0) {
        return 1.0;
    }

    double factor = 1.0;

    /* 现金分红调整 */
    if (event->cash_dividend > 0.0) {
        factor *= (close_price - event->cash_dividend) / close_price;
    }

    /* 送股调整 */
    if (event->bonus_share_ratio > 0.0) {
        factor *= 1.0 / (1.0 + event->bonus_share_ratio);
    }

    /* 配股调整 */
    if (event->rights_issue_ratio > 0.0 && event->rights_issue_price > 0.0) {
        double rights_factor = (close_price + event->rights_issue_price *
                               event->rights_issue_ratio) /
                               (close_price * (1.0 + event->rights_issue_ratio));
        factor *= rights_factor;
    }

    return factor;
}

/* 创建数据清洗器 */
data_cleaner_t* data_cleaner_create(void) {
    struct data_cleaner* cleaner = (struct data_cleaner*)malloc(sizeof(struct data_cleaner));
    if (cleaner == NULL) {
        LOG_ERROR("Failed to allocate memory for data cleaner");
        return NULL;
    }

    cleaner->dividend_events = (dividend_event_t*)malloc(
        sizeof(dividend_event_t) * MAX_DIVIDEND_EVENTS);
    if (cleaner->dividend_events == NULL) {
        LOG_ERROR("Failed to allocate dividend events array");
        free(cleaner);
        return NULL;
    }

    cleaner->dividend_count = 0;
    cleaner->dividend_capacity = MAX_DIVIDEND_EVENTS;

    LOG_INFO("Data cleaner created");
    return cleaner;
}

/* 销毁数据清洗器 */
void data_cleaner_destroy(data_cleaner_t* cleaner) {
    if (cleaner == NULL) {
        return;
    }

    if (cleaner->dividend_events != NULL) {
        free(cleaner->dividend_events);
    }

    free(cleaner);
    LOG_INFO("Data cleaner destroyed");
}

/* 注册除权除息事件 */
int data_cleaner_add_dividend(data_cleaner_t* cleaner, const dividend_event_t* event) {
    if (cleaner == NULL || event == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    if (cleaner->dividend_count >= cleaner->dividend_capacity) {
        LOG_ERROR("Dividend events array full (max %d)", cleaner->dividend_capacity);
        return GNTS_ERROR_MEMORY;
    }

    /* 按日期排序插入 */
    int insert_pos = cleaner->dividend_count;
    for (int i = 0; i < cleaner->dividend_count; i++) {
        if (event->date < cleaner->dividend_events[i].date) {
            insert_pos = i;
            break;
        }
    }

    /* 后移元素 */
    for (int i = cleaner->dividend_count; i > insert_pos; i--) {
        cleaner->dividend_events[i] = cleaner->dividend_events[i - 1];
    }

    cleaner->dividend_events[insert_pos] = *event;
    cleaner->dividend_count++;

    LOG_INFO("Dividend event added: date=%lld, cash=%.4f, bonus=%.4f",
             (long long)event->date, event->cash_dividend, event->bonus_share_ratio);
    return GNTS_OK;
}

/* 清除已注册的除权除息事件 */
void data_cleaner_clear_dividends(data_cleaner_t* cleaner) {
    if (cleaner == NULL) {
        return;
    }
    cleaner->dividend_count = 0;
    LOG_INFO("Dividend events cleared");
}

/* 处理停牌数据 */
int data_cleaner_handle_suspension(data_cleaner_t* cleaner, bar_data_t* bars, int count) {
    if (bars == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    (void)cleaner; /* 暂未使用 */

    int removed = 0;
    for (int i = 0; i < count; i++) {
        /* 检测停牌：成交量为0且价格不变 */
        if (bars[i].volume == 0 &&
            bars[i].open == bars[i].high &&
            bars[i].high == bars[i].low &&
            bars[i].low == bars[i].close) {
            /* 标记为无效数据 */
            bars[i].volume = -1; /* 用-1标记停牌 */
            removed++;
        }
    }

    LOG_INFO("Handled suspension data, marked: %d", removed);
    return GNTS_OK;
}

/* 前复权：保持最新价格不变，向前调整历史价格 */
static int adjust_front(data_cleaner_t* cleaner, bar_data_t* bars, int count) {
    if (count <= 1) {
        return GNTS_OK;
    }

    /* 从最后一根K线开始向前复权 */
    double cumulative_factor = 1.0;
    int event_idx = cleaner->dividend_count - 1; /* 从最近的除权事件开始 */

    for (int i = count - 1; i >= 0; i--) {
        /* 检查当前K线是否在除权除息日之后，如果是则更新累积因子 */
        while (event_idx >= 0 && bars[i].timestamp <= cleaner->dividend_events[event_idx].date) {
            double factor = calc_dividend_factor(
                &cleaner->dividend_events[event_idx], bars[i].close);
            cumulative_factor *= factor;
            event_idx--;

            LOG_DEBUG("Front adjust: event %d, factor=%.6f, cumulative=%.6f",
                      event_idx + 1, factor, cumulative_factor);
        }

        /* 应用累积因子 */
        bars[i].open  *= cumulative_factor;
        bars[i].high  *= cumulative_factor;
        bars[i].low   *= cumulative_factor;
        bars[i].close *= cumulative_factor;

        /* 成交量也需调整（送股/配股影响） */
        if (cumulative_factor != 1.0 && bars[i].volume > 0) {
            bars[i].volume = (int64_t)(bars[i].volume / cumulative_factor);
            bars[i].turnover *= cumulative_factor;
        }
    }

    LOG_INFO("Front adjustment completed with %d dividend events", cleaner->dividend_count);
    return GNTS_OK;
}

/* 后复权：保持最早价格不变，向后调整后续价格 */
static int adjust_back(data_cleaner_t* cleaner, bar_data_t* bars, int count) {
    if (count <= 1) {
        return GNTS_OK;
    }

    /* 从第一根K线开始向后复权 */
    double cumulative_factor = 1.0;
    int event_idx = 0; /* 从最早的除权事件开始 */

    for (int i = 0; i < count; i++) {
        /* 检查当前K线是否在除权除息日之后，如果是则更新累积因子 */
        /* 注意：后复权中，除权日之后的价格需要乘以调整因子的倒数 */
        while (event_idx < cleaner->dividend_count &&
               bars[i].timestamp > cleaner->dividend_events[event_idx].date) {
            double factor = calc_dividend_factor(
                &cleaner->dividend_events[event_idx], bars[i].close);
            cumulative_factor /= factor; /* 后复权使用倒数 */
            event_idx++;

            LOG_DEBUG("Back adjust: event %d, factor=%.6f, cumulative=%.6f",
                      event_idx - 1, 1.0 / factor, cumulative_factor);
        }

        /* 应用累积因子 */
        bars[i].open  *= cumulative_factor;
        bars[i].high  *= cumulative_factor;
        bars[i].low   *= cumulative_factor;
        bars[i].close *= cumulative_factor;

        /* 成交量也需调整 */
        if (cumulative_factor != 1.0 && bars[i].volume > 0) {
            bars[i].volume = (int64_t)(bars[i].volume * cumulative_factor);
            bars[i].turnover /= cumulative_factor;
        }
    }

    LOG_INFO("Back adjustment completed with %d dividend events", cleaner->dividend_count);
    return GNTS_OK;
}

/* 复权处理 */
int data_cleaner_adjust_price(data_cleaner_t* cleaner, bar_data_t* bars, int count,
                               adjust_type_t type) {
    if (cleaner == NULL || bars == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    switch (type) {
        case ADJUST_TYPE_NONE:
            return GNTS_OK;
        case ADJUST_TYPE_FRONT:
            return adjust_front(cleaner, bars, count);
        case ADJUST_TYPE_BACK:
            return adjust_back(cleaner, bars, count);
        default:
            LOG_ERROR("Invalid adjust type: %d", type);
            return GNTS_ERROR_INVALID_PARAM;
    }
}

/* 去除异常数据 */
int data_cleaner_remove_outliers(data_cleaner_t* cleaner, bar_data_t* bars, int count,
                                  double threshold) {
    if (bars == NULL || count <= 0 || threshold <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    (void)cleaner; /* 暂未使用 */

    /* 计算收益率均值和标准差 */
    double sum_return = 0.0;
    int return_count = 0;

    for (int i = 1; i < count; i++) {
        if (bars[i].close > 0 && bars[i-1].close > 0) {
            double ret = (bars[i].close - bars[i-1].close) / bars[i-1].close;
            sum_return += ret;
            return_count++;
        }
    }

    if (return_count == 0) {
        LOG_WARNING("No valid data for outlier detection");
        return GNTS_OK;
    }

    double mean_return = sum_return / return_count;

    /* 计算标准差 */
    double sum_sq = 0.0;
    for (int i = 1; i < count; i++) {
        if (bars[i].close > 0 && bars[i-1].close > 0) {
            double ret = (bars[i].close - bars[i-1].close) / bars[i-1].close;
            sum_sq += (ret - mean_return) * (ret - mean_return);
        }
    }

    double std_return = sqrt(sum_sq / return_count);

    /* 标记异常数据 */
    int removed = 0;
    for (int i = 1; i < count; i++) {
        if (bars[i].close > 0 && bars[i-1].close > 0) {
            double ret = (bars[i].close - bars[i-1].close) / bars[i-1].close;
            double zscore = fabs(ret - mean_return) / std_return;

            if (zscore > threshold) {
                /* 标记为异常 */
                bars[i].volume = -2; /* 用-2标记异常 */
                removed++;
            }
        }
    }

    LOG_INFO("Removed %d outliers with threshold %.2f", removed, threshold);
    return GNTS_OK;
}

/* 填充缺失数据 */
int data_cleaner_fill_missing(data_cleaner_t* cleaner, bar_data_t* bars, int count) {
    if (bars == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    (void)cleaner; /* 暂未使用 */

    int filled = 0;

    /* 向前填充 */
    for (int i = 1; i < count; i++) {
        /* 检测缺失：成交量为0但价格有效，或标记为异常 */
        if ((bars[i].volume == 0 && bars[i].close > 0) || bars[i].volume == -1) {
            /* 使用前一根K线的数据填充 */
            if (bars[i-1].volume > 0) {
                bars[i].open = bars[i-1].close;
                bars[i].high = bars[i-1].close;
                bars[i].low = bars[i-1].close;
                bars[i].close = bars[i-1].close;
                bars[i].volume = bars[i-1].volume;
                bars[i].turnover = bars[i-1].turnover;
                filled++;
            }
        }
    }

    LOG_INFO("Filled %d missing bars", filled);
    return GNTS_OK;
}