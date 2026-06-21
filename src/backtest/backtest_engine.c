/*
 * GNTS - 观澜 · Nexus Trading System
 * 回测引擎实现
 */

#include "backtest/backtest_engine.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 回测引擎内部结构 */
struct backtest_engine_impl {
    backtest_config_t config;
    backtest_account_t account;
    strategy_interface_t* strategy;
    bar_data_t** data_bars;
    int* bar_counts;
    char** symbols;
    int symbol_count;
    int is_running;
    int current_index;

    /* 交易记录 */
    order_response_t* orders;
    int order_count;
    int max_orders;
    trade_record_t* trades;
    int trade_count;
    int max_trades;

    /* 资金曲线 */
    double* equity_curve;
    int equity_count;
    int max_equity;
};

/* 创建回测引擎 */
backtest_engine_t* backtest_engine_create(const backtest_config_t* config) {
    if (config == NULL) {
        LOG_ERROR("Invalid config for backtest engine");
        return NULL;
    }

    struct backtest_engine_impl* engine = (struct backtest_engine_impl*)malloc(sizeof(struct backtest_engine_impl));
    if (engine == NULL) {
        LOG_ERROR("Failed to allocate memory for backtest engine");
        return NULL;
    }

    engine->config = *config;
    engine->strategy = NULL;
    engine->data_bars = NULL;
    engine->bar_counts = NULL;
    engine->symbols = NULL;
    engine->symbol_count = 0;
    engine->is_running = 0;
    engine->current_index = 0;

    /* 初始化账户 */
    engine->account.initial_capital = config->initial_capital;
    engine->account.cash = config->initial_capital;
    engine->account.frozen_cash = 0.0;
    engine->account.market_value = 0.0;
    engine->account.total_asset = config->initial_capital;
    engine->account.total_profit_loss = 0.0;
    engine->account.realized_profit_loss = 0.0;
    engine->account.unrealized_profit_loss = 0.0;
    engine->account.positions = NULL;
    engine->account.position_count = 0;
    engine->account.max_positions = 100;

    /* 初始化订单和成交记录（动态扩容） */
    engine->orders = NULL;
    engine->order_count = 0;
    engine->max_orders = 0;

    engine->trades = NULL;
    engine->trade_count = 0;
    engine->max_trades = 0;

    /* 初始化资金曲线（动态扩容） */
    engine->equity_curve = NULL;
    engine->equity_count = 0;
    engine->max_equity = 0;

    LOG_INFO("Backtest engine created with initial capital %.2f", config->initial_capital);
    return (backtest_engine_t*)engine;
}

/* 销毁回测引擎 */
void backtest_engine_destroy(backtest_engine_t* engine) {
    if (engine == NULL) {
        return;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;

    /* 释放数据 */
    if (impl->data_bars != NULL) {
        for (int i = 0; i < impl->symbol_count; i++) {
            free(impl->data_bars[i]);
        }
        free(impl->data_bars);
        free(impl->bar_counts);
        free(impl->symbols);
    }

    /* 释放账户持仓 */
    if (impl->account.positions != NULL) {
        free(impl->account.positions);
    }

    /* 释放订单和成交记录 */
    if (impl->orders != NULL) {
        free(impl->orders);
    }
    if (impl->trades != NULL) {
        free(impl->trades);
    }

    /* 释放资金曲线 */
    if (impl->equity_curve != NULL) {
        free(impl->equity_curve);
    }

    free(impl);
    LOG_INFO("Backtest engine destroyed");
}

/* 设置策略 */
int backtest_engine_set_strategy(backtest_engine_t* engine, strategy_interface_t* strategy) {
    if (engine == NULL || strategy == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    impl->strategy = strategy;

    LOG_INFO("Strategy %s set for backtest", strategy->name);
    return GNTS_OK;
}

/* 加载历史数据 */
int backtest_engine_load_data(backtest_engine_t* engine, const char* symbol,
                              const bar_data_t* bars, int count) {
    if (engine == NULL || symbol == NULL || bars == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;

    /* 扩容 */
    int new_count = impl->symbol_count + 1;
    bar_data_t** new_bars = (bar_data_t**)realloc(impl->data_bars, sizeof(bar_data_t*) * new_count);
    int* new_bar_counts = (int*)realloc(impl->bar_counts, sizeof(int) * new_count);
    char** new_symbols = (char**)realloc(impl->symbols, sizeof(char*) * new_count);

    if (new_bars == NULL || new_bar_counts == NULL || new_symbols == NULL) {
        LOG_ERROR("Failed to expand data arrays");
        return GNTS_ERROR_MEMORY;
    }

    impl->data_bars = new_bars;
    impl->bar_counts = new_bar_counts;
    impl->symbols = new_symbols;

    /* 复制数据 */
    impl->data_bars[impl->symbol_count] = (bar_data_t*)malloc(sizeof(bar_data_t) * count);
    if (impl->data_bars[impl->symbol_count] == NULL) {
        LOG_ERROR("Failed to allocate memory for bars");
        return GNTS_ERROR_MEMORY;
    }

    memcpy(impl->data_bars[impl->symbol_count], bars, sizeof(bar_data_t) * count);
    impl->bar_counts[impl->symbol_count] = count;

    impl->symbols[impl->symbol_count] = strdup(symbol);
    impl->symbol_count++;

    LOG_INFO("Loaded %d bars for %s", count, symbol);
    return GNTS_OK;
}

/* 计算手续费 */
static double calculate_commission(const backtest_config_t* config, double value) {
    double commission = 0.0;

    switch (config->commission.type) {
        case COMMISSION_TYPE_FIXED:
            commission = config->commission.value;
            break;
        case COMMISSION_TYPE_RATIO:
            commission = value * config->commission.value;
            if (commission < config->commission.min_commission) {
                commission = config->commission.min_commission;
            }
            break;
        case COMMISSION_TYPE_TIERED:
            /* 分段收费实现 */
            commission = value * 0.0003; /* 暂时简化 */
            break;
    }

    return commission;
}

/* 计算滑点 */
static double calculate_slippage(const backtest_config_t* config, double price, int64_t quantity) {
    double slippage = 0.0;

    switch (config->slippage.type) {
        case SLIPPAGE_TYPE_FIXED:
            slippage = config->slippage.value;
            break;
        case SLIPPAGE_TYPE_RATIO:
            slippage = price * config->slippage.value;
            break;
        case SLIPPAGE_TYPE_PRICE_IMPACT:
            slippage = price * config->slippage.price_impact_coefficient * quantity;
            break;
    }

    return slippage;
}

/* 处理订单 */
static int process_order(struct backtest_engine_impl* impl, const bar_data_t* bar) {
    if (impl->strategy == NULL || impl->strategy->generate_signal == NULL) {
        return GNTS_OK;
    }

    /* 生成信号 */
    signal_t signal = impl->strategy->generate_signal(impl->strategy, bar->symbol);

    if (signal == SIGNAL_NONE || signal == SIGNAL_HOLD) {
        return GNTS_OK;
    }

    /* 创建订单 */
    order_request_t request;
    snprintf(request.order_id, sizeof(request.order_id), "BT_%d", impl->order_count);
    strncpy(request.symbol, bar->symbol, SYMBOL_MAX_LEN - 1);
    request.direction = (signal == SIGNAL_BUY) ? ORDER_BUY : ORDER_SELL;
    request.type = ORDER_TYPE_LIMIT;
    request.price = bar->close;
    /* 按可用资金的10%计算交易数量 */
    request.quantity = (int64_t)((impl->account.cash * 0.1) / bar->close);
    if (request.quantity < 100) request.quantity = 100; /* 最小交易单位 */
    request.create_time = bar->timestamp;
    strncpy(request.strategy_id, impl->strategy->id, sizeof(request.strategy_id) - 1);

    /* 计算成本 */
    double order_value = request.price * request.quantity;
    double commission = calculate_commission(&impl->config, order_value);
    double slippage = calculate_slippage(&impl->config, request.price, request.quantity);

    /* 调整价格（考虑滑点） */
    if (request.direction == ORDER_BUY) {
        request.price += slippage;
    } else {
        request.price -= slippage;
    }

    /* 检查资金 */
    double total_cost = order_value + commission;
    if (request.direction == ORDER_BUY && impl->account.cash < total_cost) {
        LOG_WARNING("Insufficient cash for buy order");
        return GNTS_OK;
    }

    /* 检查持仓 */
    if (request.direction == ORDER_SELL) {
        position_t* pos = NULL;
        for (int i = 0; i < impl->account.position_count; i++) {
            if (strcmp(impl->account.positions[i].symbol, bar->symbol) == 0) {
                pos = &impl->account.positions[i];
                break;
            }
        }

        if (pos == NULL || pos->available < request.quantity) {
            LOG_WARNING("Insufficient position for sell order");
            return GNTS_OK;
        }
    }

    /* 模拟成交 */
    order_response_t response;
    strncpy(response.order_id, request.order_id, sizeof(response.order_id) - 1);
    strncpy(response.symbol, request.symbol, SYMBOL_MAX_LEN - 1);
    response.direction = request.direction;
    response.type = request.type;
    response.price = request.price;
    response.quantity = request.quantity;
    response.filled_quantity = request.quantity;
    response.filled_price = request.price;
    response.status = ORDER_STATUS_FILLED;
    response.create_time = request.create_time;
    response.update_time = request.create_time;
    strncpy(response.strategy_id, request.strategy_id, sizeof(response.strategy_id) - 1);
    response.error_msg[0] = '\0';

    /* 保存订单（动态扩容） */
    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->orders, impl->order_count, impl->max_orders,
                                  sizeof(order_response_t), 256);
    impl->orders[impl->order_count++] = response;

    /* 创建成交记录 */
    trade_record_t trade;
    snprintf(trade.trade_id, sizeof(trade.trade_id), "TR_%d", impl->trade_count);
    strncpy(trade.order_id, request.order_id, sizeof(trade.order_id) - 1);
    strncpy(trade.symbol, request.symbol, SYMBOL_MAX_LEN - 1);
    trade.direction = request.direction;
    trade.price = request.price;
    trade.quantity = request.quantity;
    trade.trade_time = request.create_time;
    strncpy(trade.strategy_id, request.strategy_id, sizeof(trade.strategy_id) - 1);

    /* 保存成交记录（动态扩容） */
    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->trades, impl->trade_count, impl->max_trades,
                                  sizeof(trade_record_t), 256);
    impl->trades[impl->trade_count++] = trade;

    /* 更新账户 */
    if (request.direction == ORDER_BUY) {
        /* 买入：减少现金，增加持仓 */
        impl->account.cash -= total_cost;

        /* 更新持仓 */
        position_t* pos = NULL;
        for (int i = 0; i < impl->account.position_count; i++) {
            if (strcmp(impl->account.positions[i].symbol, bar->symbol) == 0) {
                pos = &impl->account.positions[i];
                break;
            }
        }

        if (pos == NULL) {
            /* 新建持仓 */
            if (impl->account.position_count >= impl->account.max_positions) {
                LOG_WARNING("Max positions reached");
                return GNTS_OK;
            }

            if (impl->account.positions == NULL) {
                impl->account.positions = (position_t*)malloc(sizeof(position_t) * impl->account.max_positions);
            }

            pos = &impl->account.positions[impl->account.position_count++];
            strncpy(pos->symbol, bar->symbol, SYMBOL_MAX_LEN - 1);
            pos->position = request.quantity;
            pos->available = request.quantity;
            pos->cost_price = request.price;
            pos->market_value = request.price * request.quantity;
            pos->profit_loss = 0.0;
            pos->profit_loss_ratio = 0.0;
            pos->update_time = bar->timestamp;
        } else {
            /* 增加持仓 */
            double new_cost = pos->cost_price * pos->position + request.price * request.quantity;
            pos->position += request.quantity;
            pos->available += request.quantity;
            pos->cost_price = new_cost / pos->position;
            pos->market_value = request.price * pos->position;
            pos->update_time = bar->timestamp;
        }
    } else {
        /* 卖出：增加现金，减少持仓 */
        double sell_value = request.price * request.quantity;
        double sell_commission = calculate_commission(&impl->config, sell_value);
        double stamp_duty = sell_value * impl->config.stamp_duty;

        impl->account.cash += sell_value - sell_commission - stamp_duty;

        /* 更新持仓 */
        position_t* pos = NULL;
        for (int i = 0; i < impl->account.position_count; i++) {
            if (strcmp(impl->account.positions[i].symbol, bar->symbol) == 0) {
                pos = &impl->account.positions[i];
                break;
            }
        }

        if (pos != NULL) {
            /* 计算已实现盈亏 */
            double realized_pl = (request.price - pos->cost_price) * request.quantity;
            impl->account.realized_profit_loss += realized_pl;

            /* 减少持仓 */
            pos->position -= request.quantity;
            pos->available -= request.quantity;

            if (pos->position == 0) {
                /* 清空持仓 */
                for (int i = 0; i < impl->account.position_count - 1; i++) {
                    impl->account.positions[i] = impl->account.positions[i + 1];
                }
                impl->account.position_count--;
            } else {
                pos->market_value = pos->cost_price * pos->position;
                pos->update_time = bar->timestamp;
            }
        }
    }

    /* 调用策略回调 */
    if (impl->strategy->on_order != NULL) {
        impl->strategy->on_order(impl->strategy, &response);
    }
    if (impl->strategy->on_trade != NULL) {
        impl->strategy->on_trade(impl->strategy, &trade);
    }

    return GNTS_OK;
}

/* 更新持仓市值（使用各 symbol 的最新已知价格） */
static void update_positions(struct backtest_engine_impl* impl, const bar_data_t* bar) {
    /* 首先用当前 bar 更新匹配的持仓 */
    for (int i = 0; i < impl->account.position_count; i++) {
        position_t* pos = &impl->account.positions[i];

        if (strcmp(pos->symbol, bar->symbol) == 0) {
            pos->market_value = bar->close * pos->position;
            pos->profit_loss = (bar->close - pos->cost_price) * pos->position;
            if (pos->cost_price != 0.0) {
                pos->profit_loss_ratio = (bar->close - pos->cost_price) / pos->cost_price;
            }
            pos->update_time = bar->timestamp;
        }
        /* 非匹配持仓保持最近一次更新时的市值不变 */
    }

    /* 汇总所有持仓的最新市值和未实现盈亏 */
    impl->account.market_value = 0.0;
    impl->account.unrealized_profit_loss = 0.0;

    for (int i = 0; i < impl->account.position_count; i++) {
        impl->account.market_value += impl->account.positions[i].market_value;
        impl->account.unrealized_profit_loss += impl->account.positions[i].profit_loss;
    }

    impl->account.total_asset = impl->account.cash + impl->account.market_value;
    impl->account.total_profit_loss = impl->account.total_asset - impl->account.initial_capital;
}

/* 运行回测 */
int backtest_engine_run(backtest_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;

    if (impl->strategy == NULL) {
        LOG_ERROR("No strategy set for backtest");
        return GNTS_ERROR;
    }

    if (impl->symbol_count == 0) {
        LOG_ERROR("No data loaded for backtest");
        return GNTS_ERROR;
    }

    LOG_INFO("Starting backtest from %lld to %lld",
             impl->config.start_time, impl->config.end_time);

    /* 初始化策略 */
    if (impl->strategy->on_init != NULL) {
        strategy_context_t ctx;
        ctx.data_manager = NULL;
        ctx.order_manager = NULL;
        ctx.risk_manager = NULL;
        ctx.user_data = NULL;

        int ret = impl->strategy->on_init(impl->strategy, &ctx);
        if (ret != GNTS_OK) {
            LOG_ERROR("Failed to initialize strategy");
            return ret;
        }
    }

    /* 启动策略 */
    if (impl->strategy->on_start != NULL) {
        impl->strategy->on_start(impl->strategy);
    }

    impl->strategy->status = STRATEGY_STATUS_RUNNING;
    impl->is_running = 1;

    /* 逐Bar回测 */
    int max_bars = 0;
    for (int i = 0; i < impl->symbol_count; i++) {
        if (impl->bar_counts[i] > max_bars) {
            max_bars = impl->bar_counts[i];
        }
    }

    for (int i = 0; i < max_bars; i++) {
        for (int j = 0; j < impl->symbol_count; j++) {
            if (i < impl->bar_counts[j]) {
                const bar_data_t* bar = &impl->data_bars[j][i];

                /* 检查时间范围 */
                if (bar->timestamp < impl->config.start_time ||
                    bar->timestamp > impl->config.end_time) {
                    continue;
                }

                /* 更新持仓市值 */
                update_positions(impl, bar);

                /* 记录资金曲线（动态扩容） */
                DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->equity_curve, impl->equity_count,
                                              impl->max_equity, sizeof(double), 1024);
                impl->equity_curve[impl->equity_count++] = impl->account.total_asset;

                /* 调用策略回调 */
                if (impl->strategy->on_bar != NULL) {
                    impl->strategy->on_bar(impl->strategy, bar);
                }

                /* 处理订单 */
                process_order(impl, bar);
            }
        }
    }

    /* 停止策略 */
    if (impl->strategy->on_stop != NULL) {
        impl->strategy->on_stop(impl->strategy);
    }

    impl->strategy->status = STRATEGY_STATUS_STOPPED;
    impl->is_running = 0;

    LOG_INFO("Backtest completed, total trades: %d, total return: %.2f%%",
             impl->trade_count,
             (impl->account.total_asset / impl->account.initial_capital - 1.0) * 100.0);

    return GNTS_OK;
}

/* 暂停回测 */
int backtest_engine_pause(backtest_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    impl->is_running = 0;

    LOG_INFO("Backtest paused");
    return GNTS_OK;
}

/* 恢复回测 */
int backtest_engine_resume(backtest_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    impl->is_running = 1;

    LOG_INFO("Backtest resumed");
    return GNTS_OK;
}

/* 停止回测 */
int backtest_engine_stop(backtest_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;

    if (impl->strategy != NULL && impl->strategy->on_stop != NULL) {
        impl->strategy->on_stop(impl->strategy);
    }

    impl->is_running = 0;

    LOG_INFO("Backtest stopped");
    return GNTS_OK;
}

/* 获取回测结果 */
int backtest_engine_get_statistics(backtest_engine_t* engine, backtest_statistics_t* stats) {
    if (engine == NULL || stats == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;

    /* 计算统计指标 */
    stats->total_return = impl->account.total_asset / impl->account.initial_capital - 1.0;

    /* 计算最大回撤 */
    double max_equity = impl->account.initial_capital;
    double max_drawdown = 0.0;

    for (int i = 0; i < impl->equity_count; i++) {
        if (impl->equity_curve[i] > max_equity) {
            max_equity = impl->equity_curve[i];
        }

        double drawdown = (max_equity - impl->equity_curve[i]) / max_equity;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
    }

    stats->max_drawdown = max_drawdown;

    /* 计算交易统计（通过配对买入-卖出来计算每笔交易的盈亏） */
    stats->total_trades = impl->trade_count;
    stats->win_trades = 0;
    stats->loss_trades = 0;
    double total_profit = 0.0;
    double total_loss = 0.0;

    /* 按订单配对：买入-卖出配对计算盈亏 */
    double buy_price = 0.0;
    int pending_buy = 0;

    for (int i = 0; i < impl->trade_count; i++) {
        if (impl->trades[i].direction == ORDER_BUY) {
            buy_price = impl->trades[i].price;
            pending_buy = 1;
        } else if (impl->trades[i].direction == ORDER_SELL && pending_buy) {
            double pnl = (impl->trades[i].price - buy_price) * impl->trades[i].quantity;
            if (pnl > 0) {
                stats->win_trades++;
                total_profit += pnl;
            } else {
                stats->loss_trades++;
                total_loss += -pnl;
            }
            pending_buy = 0;
        }
    }

    if (stats->win_trades > 0)
        stats->avg_profit = total_profit / stats->win_trades;
    if (stats->loss_trades > 0)
        stats->avg_loss = total_loss / stats->loss_trades;
    stats->win_rate = stats->total_trades > 0 ?
        (double)stats->win_trades / stats->total_trades : 0.0;

    /* 年化收益率（假设回测时间为1年，精确计算需根据实际时间跨度） */
    stats->annual_return = stats->total_return;

    /* 夏普比率（基于资金曲线日收益率计算，比硬编码更准确） */
    if (impl->equity_count > 1) {
        double sum_ret = 0.0, sum_sq = 0.0;
        int ret_count = 0;
        for (int i = 1; i < impl->equity_count; i++) {
            if (impl->equity_curve[i-1] > 0) {
                double daily_ret = (impl->equity_curve[i] - impl->equity_curve[i-1])
                                   / impl->equity_curve[i-1];
                sum_ret += daily_ret;
                sum_sq += daily_ret * daily_ret;
                ret_count++;
            }
        }
        if (ret_count > 1) {
            double mean_ret = sum_ret / ret_count;
            double var = (sum_sq / (ret_count - 1)) - (mean_ret * mean_ret * ret_count / (ret_count - 1));
            if (var > 0) {
                double std_ret = sqrt(var);
                stats->sharpe_ratio = std_ret > 0 ? (mean_ret / std_ret) * sqrt(252.0) : 0.0;
            } else {
                stats->sharpe_ratio = 0.0;
            }
        }
    }

    LOG_INFO("Backtest statistics calculated");
    return GNTS_OK;
}

/* 获取交易记录 */
int backtest_engine_get_trades(backtest_engine_t* engine, trade_record_t** trades, int* count) {
    if (engine == NULL || trades == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    *trades = impl->trades;
    *count = impl->trade_count;

    return GNTS_OK;
}

/* 获取订单记录 */
int backtest_engine_get_orders(backtest_engine_t* engine, order_response_t** orders, int* count) {
    if (engine == NULL || orders == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    *orders = impl->orders;
    *count = impl->order_count;

    return GNTS_OK;
}

/* 获取资金曲线 */
int backtest_engine_get_equity_curve(backtest_engine_t* engine, double** curve, int* count) {
    if (engine == NULL || curve == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct backtest_engine_impl* impl = (struct backtest_engine_impl*)engine;
    *curve = impl->equity_curve;
    *count = impl->equity_count;

    return GNTS_OK;
}