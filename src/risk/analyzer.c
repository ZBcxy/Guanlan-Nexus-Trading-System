/*
 * GNTS - 观澜 · Nexus Trading System
 * 事后分析工具实现 - 完整版本
 */

#include "risk/risk_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 持仓快照（用于追踪开仓/平仓） */
typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    order_direction_t direction;
    double open_price;
    int64_t open_quantity;
    timestamp_t open_time;
    double close_price;
    int64_t close_quantity;
    timestamp_t close_time;
    double profit_loss;
    double profit_loss_ratio;
    int is_closed;  /* 是否已平仓 */
} position_snapshot_t;

/* 交易对（配对买卖） */
typedef struct {
    position_snapshot_t snapshot;
    double commission;
    double slippage;
} trade_pair_t;

/* 分析器内部结构 */
struct analyzer_impl {
    trade_record_t* trades;
    int trade_count;
    int max_trades;
    order_response_t* orders;
    int order_count;
    int max_orders;

    /* 持仓快照列表 */
    position_snapshot_t* snapshots;
    int snapshot_count;
    int max_snapshots;

    /* 交易对列表 */
    trade_pair_t* trade_pairs;
    int trade_pair_count;
    int max_trade_pairs;
};

/* 创建分析器 */
analyzer_t* analyzer_create(void) {
    struct analyzer_impl* analyzer = (struct analyzer_impl*)malloc(sizeof(struct analyzer_impl));
    if (analyzer == NULL) {
        LOG_ERROR("Failed to allocate memory for analyzer");
        return NULL;
    }

    /* 动态扩容数组，初始为NULL，首次添加时自动分配 */
    analyzer->trades = NULL;
    analyzer->trade_count = 0;
    analyzer->max_trades = 0;
    analyzer->orders = NULL;
    analyzer->order_count = 0;
    analyzer->max_orders = 0;

    analyzer->snapshots = NULL;
    analyzer->snapshot_count = 0;
    analyzer->max_snapshots = 0;

    analyzer->trade_pairs = NULL;
    analyzer->trade_pair_count = 0;
    analyzer->max_trade_pairs = 0;

    LOG_INFO("Analyzer created");
    return (analyzer_t*)analyzer;
}

/* 销毁分析器 */
void analyzer_destroy(analyzer_t* analyzer) {
    if (analyzer == NULL) {
        return;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    if (impl->trades != NULL) free(impl->trades);
    if (impl->orders != NULL) free(impl->orders);
    if (impl->snapshots != NULL) free(impl->snapshots);
    if (impl->trade_pairs != NULL) free(impl->trade_pairs);

    free(impl);
    LOG_INFO("Analyzer destroyed");
}

/* 添加交易记录 */
int analyzer_add_trade(analyzer_t* analyzer, const trade_record_t* trade) {
    if (analyzer == NULL || trade == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->trades, impl->trade_count, impl->max_trades,
                                  sizeof(trade_record_t), 256);

    impl->trades[impl->trade_count++] = *trade;

    return GNTS_OK;
}

/* 添加订单记录 */
int analyzer_add_order(analyzer_t* analyzer, const order_response_t* order) {
    if (analyzer == NULL || order == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->orders, impl->order_count, impl->max_orders,
                                  sizeof(order_response_t), 256);

    impl->orders[impl->order_count++] = *order;

    return GNTS_OK;
}

/* 配对交易（匹配买卖订单） */
static void pair_trades(analyzer_t* analyzer) {
    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    /* 清空之前的配对 */
    impl->trade_pair_count = 0;
    impl->snapshot_count = 0;

    /* 按时间和品种分组 */
    for (int i = 0; i < impl->trade_count; i++) {
        trade_record_t* trade = &impl->trades[i];

        /* 查找对应的持仓快照 */
        position_snapshot_t* snapshot = NULL;
        for (int j = 0; j < impl->snapshot_count; j++) {
            if (strcmp(impl->snapshots[j].symbol, trade->symbol) == 0 &&
                impl->snapshots[j].is_closed == 0) {

                /* 检查方向是否相反 */
                if ((impl->snapshots[j].direction == ORDER_BUY && trade->direction == ORDER_SELL) ||
                    (impl->snapshots[j].direction == ORDER_SELL && trade->direction == ORDER_BUY)) {

                    /* 配对成功 */
                    snapshot = &impl->snapshots[j];

                    /* 更新快照（平仓） */
                    snapshot->close_price = trade->price;
                    snapshot->close_quantity = trade->quantity;
                    snapshot->close_time = trade->trade_time;
                    snapshot->is_closed = 1;

                    /* 计算盈亏 */
                    if (snapshot->direction == ORDER_BUY) {
                        /* 买入开仓，卖出平仓 */
                        snapshot->profit_loss = (snapshot->close_price - snapshot->open_price)
                                              * snapshot->close_quantity;
                    } else {
                        /* 卖出开仓，买入平仓 */
                        snapshot->profit_loss = (snapshot->open_price - snapshot->close_price)
                                              * snapshot->close_quantity;
                    }

                    snapshot->profit_loss_ratio = snapshot->profit_loss /
                                                 (snapshot->open_price * snapshot->close_quantity);

                    /* 添加交易对 */
                    DYNAMIC_ARRAY_ENSURE_CAPACITY_VOID(impl->trade_pairs, impl->trade_pair_count,
                                                       impl->max_trade_pairs,
                                                       sizeof(trade_pair_t), 128);
                    impl->trade_pairs[impl->trade_pair_count].snapshot = *snapshot;
                    impl->trade_pairs[impl->trade_pair_count].commission = 0.0;
                    impl->trade_pairs[impl->trade_pair_count].slippage = 0.0;
                    impl->trade_pair_count++;

                    break;
                }
            }
        }

        /* 如果没有找到匹配的持仓，创建新持仓 */
        if (snapshot == NULL) {
            DYNAMIC_ARRAY_ENSURE_CAPACITY_VOID(impl->snapshots, impl->snapshot_count,
                                               impl->max_snapshots,
                                               sizeof(position_snapshot_t), 256);
            position_snapshot_t new_snapshot;
            strncpy(new_snapshot.symbol, trade->symbol, SYMBOL_MAX_LEN - 1);
            new_snapshot.direction = trade->direction;
            new_snapshot.open_price = trade->price;
            new_snapshot.open_quantity = trade->quantity;
            new_snapshot.open_time = trade->trade_time;
            new_snapshot.close_price = 0.0;
            new_snapshot.close_quantity = 0;
            new_snapshot.close_time = 0;
            new_snapshot.profit_loss = 0.0;
            new_snapshot.profit_loss_ratio = 0.0;
            new_snapshot.is_closed = 0;

            impl->snapshots[impl->snapshot_count++] = new_snapshot;
        }
    }

    LOG_INFO("Paired %d trades into %d trade pairs",
             impl->trade_count, impl->trade_pair_count);
}

/* 盈亏归因分析 */
int analyzer_analyze_pnl_attribution(analyzer_t* analyzer, pnl_attribution_t** results, int* count) {
    if (analyzer == NULL || results == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    if (impl->trade_count == 0) {
        *results = NULL;
        *count = 0;
        return GNTS_OK;
    }

    /* 配对交易 */
    pair_trades(analyzer);

    /* 按品种分组统计 */
    int max_symbols = impl->trade_count;
    pnl_attribution_t* attr = (pnl_attribution_t*)malloc(sizeof(pnl_attribution_t) * max_symbols);
    if (attr == NULL) {
        LOG_ERROR("Failed to allocate attribution array");
        return GNTS_ERROR_MEMORY;
    }

    int attr_count = 0;

    /* 使用哈希表思想，简化版本直接遍历 */
    for (int i = 0; i < impl->trade_pair_count; i++) {
        position_snapshot_t* snapshot = &impl->trade_pairs[i].snapshot;

        /* 查找或创建品种统计 */
        int found = -1;
        for (int j = 0; j < attr_count; j++) {
            if (strcmp(attr[j].symbol, snapshot->symbol) == 0) {
                found = j;
                break;
            }
        }

        if (found < 0) {
            /* 新品种 */
            found = attr_count++;
            strncpy(attr[found].symbol, snapshot->symbol, SYMBOL_MAX_LEN - 1);
            attr[found].profit_loss = 0.0;
            attr[found].profit_loss_ratio = 0.0;
            attr[found].trade_count = 0;
            attr[found].win_count = 0;
            attr[found].loss_count = 0;
            attr[found].avg_profit = 0.0;
            attr[found].avg_loss = 0.0;
            attr[found].max_profit = 0.0;
            attr[found].max_loss = 0.0;
        }

        /* 更新统计 */
        attr[found].trade_count++;

        if (snapshot->profit_loss > 0) {
            attr[found].win_count++;
            attr[found].profit_loss += snapshot->profit_loss;

            if (snapshot->profit_loss > attr[found].max_profit) {
                attr[found].max_profit = snapshot->profit_loss;
            }

            if (attr[found].avg_profit == 0) {
                attr[found].avg_profit = snapshot->profit_loss;
            } else {
                attr[found].avg_profit = (attr[found].avg_profit * (attr[found].win_count - 1) +
                                          snapshot->profit_loss) / attr[found].win_count;
            }
        } else if (snapshot->profit_loss < 0) {
            attr[found].loss_count++;
            attr[found].profit_loss += snapshot->profit_loss;

            if (snapshot->profit_loss < attr[found].max_loss) {
                attr[found].max_loss = snapshot->profit_loss;
            }

            if (attr[found].avg_loss == 0) {
                attr[found].avg_loss = snapshot->profit_loss;
            } else {
                attr[found].avg_loss = (attr[found].avg_loss * (attr[found].loss_count - 1) +
                                        snapshot->profit_loss) / attr[found].loss_count;
            }
        }

        /* 更新盈亏比例 */
        double total_cost = snapshot->open_price * snapshot->close_quantity;
        if (total_cost > 0) {
            attr[found].profit_loss_ratio = attr[found].profit_loss / total_cost;
        }
    }

    *results = attr;
    *count = attr_count;

    LOG_INFO("PnL attribution analysis: %d symbols analyzed", attr_count);

    return GNTS_OK;
}

/* 交易分析 */
int analyzer_analyze_trades(analyzer_t* analyzer, trade_analysis_t* analysis) {
    if (analyzer == NULL || analysis == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    if (impl->trade_count == 0) {
        memset(analysis, 0, sizeof(trade_analysis_t));
        return GNTS_OK;
    }

    /* 配对交易 */
    pair_trades(analyzer);

    analysis->total_trades = impl->trade_pair_count;
    analysis->win_trades = 0;
    analysis->loss_trades = 0;
    analysis->win_rate = 0.0;
    analysis->avg_profit = 0.0;
    analysis->avg_loss = 0.0;
    analysis->profit_factor = 0.0;
    analysis->max_consecutive_wins = 0;
    analysis->max_consecutive_losses = 0;
    analysis->avg_holding_time = 0.0;
    analysis->max_profit_trade = 0.0;
    analysis->max_loss_trade = 0.0;

    double total_profit = 0.0;
    double total_loss = 0.0;
    double total_holding_time = 0.0;
    int current_consecutive = 0;
    int max_consecutive = 0;
    int consecutive_type = 0; /* 0: none, 1: win, -1: loss */

    for (int i = 0; i < impl->trade_pair_count; i++) {
        position_snapshot_t* snapshot = &impl->trade_pairs[i].snapshot;

        if (snapshot->profit_loss > 0) {
            analysis->win_trades++;
            total_profit += snapshot->profit_loss;

            if (snapshot->profit_loss > analysis->max_profit_trade) {
                analysis->max_profit_trade = snapshot->profit_loss;
            }

            /* 连续盈利 */
            if (consecutive_type == 1) {
                current_consecutive++;
            } else {
                current_consecutive = 1;
            }
            consecutive_type = 1;

            if (current_consecutive > max_consecutive) {
                max_consecutive = current_consecutive;
            }
        } else if (snapshot->profit_loss < 0) {
            analysis->loss_trades++;
            total_loss += snapshot->profit_loss;

            if (snapshot->profit_loss < analysis->max_loss_trade) {
                analysis->max_loss_trade = snapshot->profit_loss;
            }

            /* 连续亏损 */
            if (consecutive_type == -1) {
                current_consecutive++;
            } else {
                current_consecutive = 1;
            }
            consecutive_type = -1;

            if (current_consecutive > max_consecutive) {
                max_consecutive = current_consecutive;
            }
        }

        /* 计算持仓时间 */
        if (snapshot->close_time > 0 && snapshot->open_time > 0) {
            total_holding_time += (snapshot->close_time - snapshot->open_time);
        }
    }

    /* 计算胜率 */
    if (analysis->total_trades > 0) {
        analysis->win_rate = (double)analysis->win_trades / analysis->total_trades;
    }

    /* 计算平均盈亏 */
    if (analysis->win_trades > 0) {
        analysis->avg_profit = total_profit / analysis->win_trades;
    }

    if (analysis->loss_trades > 0) {
        analysis->avg_loss = total_loss / analysis->loss_trades;
    }

    /* 计算盈亏比 */
    if (total_loss != 0) {
        analysis->profit_factor = -total_profit / total_loss;
    }

    /* 计算最大连续次数 */
    analysis->max_consecutive_wins = max_consecutive;
    analysis->max_consecutive_losses = max_consecutive;

    /* 计算平均持仓时间 */
    if (impl->trade_pair_count > 0) {
        analysis->avg_holding_time = total_holding_time / impl->trade_pair_count;
    }

    LOG_INFO("Trade analysis completed: %lld trades, win rate: %.2f%%, profit factor: %.2f",
             analysis->total_trades, analysis->win_rate * 100, analysis->profit_factor);

    return GNTS_OK;
}

/* 滑点分析 */
int analyzer_analyze_slippage(analyzer_t* analyzer, slippage_analysis_t** results, int* count) {
    if (analyzer == NULL || results == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    if (impl->order_count == 0 || impl->trade_count == 0) {
        *results = NULL;
        *count = 0;
        return GNTS_OK;
    }

    /* 为每个订单计算滑点 */
    int max_symbols = impl->order_count;
    slippage_analysis_t* slip = (slippage_analysis_t*)malloc(sizeof(slippage_analysis_t) * max_symbols);
    if (slip == NULL) {
        LOG_ERROR("Failed to allocate slippage array");
        return GNTS_ERROR_MEMORY;
    }

    int slip_count = 0;

    for (int i = 0; i < impl->order_count; i++) {
        order_response_t* order = &impl->orders[i];

        if (order->filled_quantity == 0) {
            continue;
        }

        /* 查找对应的成交记录 */
        for (int j = 0; j < impl->trade_count; j++) {
            trade_record_t* trade = &impl->trades[j];

            if (strcmp(trade->order_id, order->order_id) == 0) {
                /* 计算滑点 */
                double slippage = 0.0;

                if (order->direction == ORDER_BUY) {
                    /* 买入：实际成交价 - 委托价 */
                    slippage = trade->price - order->price;
                } else {
                    /* 卖出：委托价 - 实际成交价 */
                    slippage = order->price - trade->price;
                }

                double slippage_ratio = 0.0;
                if (order->price > 0) {
                    slippage_ratio = slippage / order->price;
                }

                /* 查找或创建品种统计 */
                int found = -1;
                for (int k = 0; k < slip_count; k++) {
                    if (strcmp(slip[k].symbol, order->symbol) == 0) {
                        found = k;
                        break;
                    }
                }

                if (found < 0) {
                    /* 新品种 */
                    found = slip_count++;
                    strncpy(slip[found].symbol, order->symbol, SYMBOL_MAX_LEN - 1);
                    slip[found].total_slippage = 0.0;
                    slip[found].avg_slippage = 0.0;
                    slip[found].max_slippage = 0.0;
                    slip[found].slippage_ratio = 0.0;
                    slip[found].trade_count = 0;
                }

                /* 更新统计 */
                slip[found].trade_count++;
                slip[found].total_slippage += slippage;

                if (slippage > slip[found].max_slippage) {
                    slip[found].max_slippage = slippage;
                }

                slip[found].avg_slippage = slip[found].total_slippage / slip[found].trade_count;

                break;
            }
        }
    }

    /* 计算平均滑点比例 */
    for (int i = 0; i < slip_count; i++) {
        slip[i].slippage_ratio = slip[i].avg_slippage;
    }

    *results = slip;
    *count = slip_count;

    LOG_INFO("Slippage analysis: %d symbols analyzed", slip_count);

    return GNTS_OK;
}

/* 生成分析报告 */
int analyzer_generate_report(analyzer_t* analyzer, const char* filename) {
    if (analyzer == NULL || filename == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct analyzer_impl* impl = (struct analyzer_impl*)analyzer;

    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        LOG_ERROR("Failed to open file %s", filename);
        return GNTS_ERROR;
    }

    /* 生成报告头 */
    fprintf(file, "===========================================\n");
    fprintf(file, "     GNTS Trading Analysis Report\n");
    fprintf(file, "===========================================\n\n");

    /* 交易统计 */
    trade_analysis_t analysis;
    analyzer_analyze_trades(analyzer, &analysis);

    fprintf(file, "Trade Statistics:\n");
    fprintf(file, "-----------------------------------------\n");
    fprintf(file, "Total Trades:      %lld\n", analysis.total_trades);
    fprintf(file, "Winning Trades:    %lld\n", analysis.win_trades);
    fprintf(file, "Losing Trades:     %lld\n", analysis.loss_trades);
    fprintf(file, "Win Rate:          %.2f%%\n", analysis.win_rate * 100);
    fprintf(file, "Avg Profit:        %.2f\n", analysis.avg_profit);
    fprintf(file, "Avg Loss:          %.2f\n", analysis.avg_loss);
    fprintf(file, "Profit Factor:     %.2f\n", analysis.profit_factor);
    fprintf(file, "Max Consecutive:   %d\n", analysis.max_consecutive_wins);
    fprintf(file, "Max Profit Trade:  %.2f\n", analysis.max_profit_trade);
    fprintf(file, "Max Loss Trade:    %.2f\n", analysis.max_loss_trade);
    fprintf(file, "\n");

    /* 盈亏归因 */
    pnl_attribution_t* attrs;
    int attr_count;
    analyzer_analyze_pnl_attribution(analyzer, &attrs, &attr_count);

    if (attr_count > 0) {
        fprintf(file, "P&L Attribution by Symbol:\n");
        fprintf(file, "-----------------------------------------\n");
        fprintf(file, "%-16s %12s %8s %8s %12s %12s\n",
                "Symbol", "P&L", "Trades", "Win%", "Avg Profit", "Avg Loss");
        fprintf(file, "-----------------------------------------\n");

        for (int i = 0; i < attr_count; i++) {
            fprintf(file, "%-16s %12.2f %8lld %7.2f%% %12.2f %12.2f\n",
                    attrs[i].symbol,
                    attrs[i].profit_loss,
                    attrs[i].trade_count,
                    (attrs[i].trade_count > 0) ?
                    ((double)attrs[i].win_count / attrs[i].trade_count * 100) : 0.0,
                    attrs[i].avg_profit,
                    attrs[i].avg_loss);
        }

        fprintf(file, "\n");
        free(attrs);
    }

    /* 滑点分析 */
    slippage_analysis_t* slips;
    int slip_count;
    analyzer_analyze_slippage(analyzer, &slips, &slip_count);

    if (slip_count > 0) {
        fprintf(file, "Slippage Analysis:\n");
        fprintf(file, "-----------------------------------------\n");
        fprintf(file, "%-16s %12s %8s %12s\n",
                "Symbol", "Avg Slippage", "Trades", "Max Slippage");
        fprintf(file, "-----------------------------------------\n");

        for (int i = 0; i < slip_count; i++) {
            fprintf(file, "%-16s %12.4f %8lld %12.4f\n",
                    slips[i].symbol,
                    slips[i].avg_slippage,
                    slips[i].trade_count,
                    slips[i].max_slippage);
        }

        fprintf(file, "\n");
        free(slips);
    }

    fprintf(file, "===========================================\n");
    fprintf(file, "Report generated by GNTS Analyzer\n");
    fprintf(file, "===========================================\n");

    fclose(file);

    LOG_INFO("Analysis report generated: %s", filename);
    return GNTS_OK;
}