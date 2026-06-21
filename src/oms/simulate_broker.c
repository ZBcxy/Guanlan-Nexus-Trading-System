/*
 * GNTS - 观澜 · Nexus Trading System
 * 模拟券商接口实现
 */

#include "oms/simulate_broker.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 生成唯一ID */
static void generate_unique_id(char* buf, size_t len, const char* prefix) {
    static int counter = 0;
    snprintf(buf, len, "%s_%ld_%d", prefix, time(NULL), counter++);
}

/* 前向声明 */
static int simulate_broker_connect(broker_interface_t* self, const broker_config_t* config);
static void simulate_broker_disconnect(broker_interface_t* self);
static int simulate_broker_is_connected(broker_interface_t* self);
static int simulate_broker_query_account(broker_interface_t* self, account_info_t* account);
static int simulate_broker_query_position(broker_interface_t* self, const char* symbol, position_t* position);
static int simulate_broker_query_positions(broker_interface_t* self, position_t** positions, int* count);
static int simulate_broker_send_order(broker_interface_t* self, const order_request_t* request, order_response_t* response);
static int simulate_broker_cancel_order(broker_interface_t* self, const char* order_id);
static int simulate_broker_query_order(broker_interface_t* self, const char* order_id, order_response_t* response);
static int simulate_broker_query_orders(broker_interface_t* self, order_response_t** orders, int* count);
static int simulate_broker_query_trade(broker_interface_t* self, const char* trade_id, trade_record_t* trade);
static int simulate_broker_query_trades(broker_interface_t* self, trade_record_t** trades, int* count);
static int simulate_broker_subscribe(broker_interface_t* self, const char* symbol);
static int simulate_broker_unsubscribe(broker_interface_t* self, const char* symbol);

/* 创建模拟券商 */
broker_interface_t* create_simulate_broker(const simulate_broker_config_t* config) {
    simulate_broker_t* broker = (simulate_broker_t*)malloc(sizeof(simulate_broker_t));
    if (broker == NULL) {
        LOG_ERROR("Failed to allocate simulate broker");
        return NULL;
    }

    /* 默认配置 */
    if (config != NULL) {
        broker->config = *config;
    } else {
        broker->config.initial_cash = 1000000.0;
        broker->config.commission_rate = 0.0003;
        broker->config.slippage_rate = 0.0001;
        broker->config.latency_ms = 100;
        broker->config.fill_probability = 90;
    }

    /* 初始化账户 */
    broker->account.cash = broker->config.initial_cash;
    broker->account.frozen_cash = 0.0;
    broker->account.total_asset = broker->config.initial_cash;
    broker->account.profit_loss = 0.0;

    /* 初始化链表 */
    broker->positions = NULL;
    broker->position_count = 0;
    broker->orders = NULL;
    broker->order_count = 0;
    broker->trades = NULL;
    broker->trade_count = 0;

    /* 初始化哈希表 */
    broker->position_table = hash_table_create(100, 100);
    broker->order_table = hash_table_create(1000, 1000);
    broker->trade_table = hash_table_create(10000, 10000);
    broker->market_data = hash_table_create(100, 100);

    /* 初始化锁 */
    pthread_mutex_init(&broker->mutex, NULL);

    broker->connected = 0;

    /* 创建接口 */
    broker_interface_t* interface = (broker_interface_t*)malloc(sizeof(broker_interface_t));
    if (interface == NULL) {
        hash_table_destroy(broker->position_table);
        hash_table_destroy(broker->order_table);
        hash_table_destroy(broker->trade_table);
        hash_table_destroy(broker->market_data);
        pthread_mutex_destroy(&broker->mutex);
        free(broker);
        return NULL;
    }

    interface->connect = simulate_broker_connect;
    interface->disconnect = simulate_broker_disconnect;
    interface->is_connected = simulate_broker_is_connected;
    interface->query_account = simulate_broker_query_account;
    interface->query_position = simulate_broker_query_position;
    interface->query_positions = simulate_broker_query_positions;
    interface->send_order = simulate_broker_send_order;
    interface->cancel_order = simulate_broker_cancel_order;
    interface->query_order = simulate_broker_query_order;
    interface->query_orders = simulate_broker_query_orders;
    interface->query_trade = simulate_broker_query_trade;
    interface->query_trades = simulate_broker_query_trades;
    interface->subscribe = simulate_broker_subscribe;
    interface->unsubscribe = simulate_broker_unsubscribe;
    interface->private_data = broker;

    LOG_INFO("Simulate broker created with initial cash: %.2f", broker->config.initial_cash);
    return interface;
}

/* 创建默认配置的模拟券商 */
broker_interface_t* create_simulate_broker_default(void) {
    return create_simulate_broker(NULL);
}

/* 销毁模拟券商 */
static void destroy_simulate_broker(simulate_broker_t* broker) {
    if (broker == NULL) {
        return;
    }

    /* 释放持仓链表 */
    simulate_position_node_t* pos = broker->positions;
    while (pos != NULL) {
        simulate_position_node_t* next = pos->next;
        free(pos);
        pos = next;
    }

    /* 释放订单链表 */
    simulate_order_node_t* order = broker->orders;
    while (order != NULL) {
        simulate_order_node_t* next = order->next;
        free(order);
        order = next;
    }

    /* 释放成交链表 */
    simulate_trade_node_t* trade = broker->trades;
    while (trade != NULL) {
        simulate_trade_node_t* next = trade->next;
        free(trade);
        trade = next;
    }

    /* 销毁哈希表 */
    hash_table_destroy(broker->position_table);
    hash_table_destroy(broker->order_table);
    hash_table_destroy(broker->trade_table);
    hash_table_destroy(broker->market_data);

    /* 销毁锁 */
    pthread_mutex_destroy(&broker->mutex);

    free(broker);
}

/* 连接券商 */
static int simulate_broker_connect(broker_interface_t* self, const broker_config_t* config) {
    if (self == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    if (broker->connected) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_WARNING("Simulate broker already connected");
        return GNTS_OK;
    }

    broker->connected = 1;

    pthread_mutex_unlock(&broker->mutex);

    LOG_INFO("Simulate broker connected");
    return GNTS_OK;
}

/* 断开连接 */
static void simulate_broker_disconnect(broker_interface_t* self) {
    if (self == NULL) {
        return;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    broker->connected = 0;

    pthread_mutex_unlock(&broker->mutex);

    LOG_INFO("Simulate broker disconnected");
}

/* 是否已连接 */
static int simulate_broker_is_connected(broker_interface_t* self) {
    if (self == NULL) {
        return 0;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;
    return broker->connected;
}

/* 查询账户 */
static int simulate_broker_query_account(broker_interface_t* self, account_info_t* account) {
    if (self == NULL || account == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    /* 计算总市值 */
    double total_market_value = 0.0;
    simulate_position_node_t* pos = broker->positions;
    while (pos != NULL) {
        total_market_value += pos->market_value;
        pos = pos->next;
    }

    account->total_asset = broker->account.cash + total_market_value;
    account->cash = broker->account.cash;
    account->market_value = total_market_value;
    account->profit_loss = account->total_asset - broker->config.initial_cash;
    account->available_cash = broker->account.cash - broker->account.frozen_cash;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询单个持仓 */
static int simulate_broker_query_position(broker_interface_t* self, const char* symbol,
                                         position_t* position) {
    if (self == NULL || symbol == NULL || position == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    /* 从哈希表查找 */
    simulate_position_node_t* node = (simulate_position_node_t*)hash_table_find(
        broker->position_table, symbol);

    if (node == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_NOT_FOUND;
    }

    position->position = node->long_position - node->short_position;
    position->available = node->long_available - node->short_available;
    position->cost_price = node->long_position > 0 ?
                           node->long_cost / node->long_position :
                           (node->short_position > 0 ? node->short_cost / node->short_position : 0.0);
    position->market_value = node->market_value;
    position->profit_loss = node->long_position > 0 ?
                           (node->market_value - node->long_cost) :
                           (node->short_position > 0 ? (node->short_cost - node->market_value) : 0.0);
    position->profit_loss_ratio = position->cost_price > 0 ?
                                 position->profit_loss / position->cost_price / position->position : 0.0;
    strncpy(position->symbol, symbol, SYMBOL_MAX_LEN - 1);

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询所有持仓 */
static int simulate_broker_query_positions(broker_interface_t* self,
                                           position_t** positions, int* count) {
    if (self == NULL || positions == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    if (broker->position_count == 0) {
        *positions = NULL;
        *count = 0;
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_OK;
    }

    *positions = (position_t*)malloc(sizeof(position_t) * broker->position_count);
    if (*positions == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_MEMORY;
    }

    int index = 0;
    simulate_position_node_t* node = broker->positions;
    while (node != NULL) {
        position_t* pos = &(*positions)[index];
        strncpy(pos->symbol, node->symbol, SYMBOL_MAX_LEN - 1);
        pos->position = node->long_position - node->short_position;
        pos->available = node->long_available - node->short_available;
        pos->cost_price = node->long_position > 0 ?
                         node->long_cost / node->long_position :
                         (node->short_position > 0 ? node->short_cost / node->short_position : 0.0);
        pos->market_value = node->market_value;
        pos->profit_loss = node->long_position > 0 ?
                         (node->market_value - node->long_cost) :
                         (node->short_position > 0 ? (node->short_cost - node->market_value) : 0.0);
        pos->profit_loss_ratio = pos->cost_price > 0 && pos->position != 0 ?
                                 pos->profit_loss / pos->cost_price / pos->position : 0.0;

        index++;
        node = node->next;
    }

    *count = broker->position_count;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 发送订单 */
static int simulate_broker_send_order(broker_interface_t* self,
                                      const order_request_t* request,
                                      order_response_t* response) {
    if (self == NULL || request == NULL || response == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    if (!broker->connected) {
        LOG_ERROR("Broker not connected");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&broker->mutex);

    /* 生成订单ID */
    char order_id[64];
    if (strlen(request->order_id) == 0) {
        generate_unique_id(order_id, sizeof(order_id), "ORD");
    } else {
        strncpy(order_id, request->order_id, sizeof(order_id) - 1);
    }

    /* 创建订单节点 */
    simulate_order_node_t* order = (simulate_order_node_t*)malloc(sizeof(simulate_order_node_t));
    if (order == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_MEMORY;
    }

    strncpy(order->order_id, order_id, sizeof(order->order_id) - 1);
    strncpy(order->symbol, request->symbol, SYMBOL_MAX_LEN - 1);
    order->direction = request->direction;
    order->type = request->type;
    order->price = request->price;
    order->quantity = request->quantity;
    order->filled_quantity = 0;
    order->avg_price = 0.0;
    order->create_time = request->create_time > 0 ? request->create_time : get_current_timestamp();
    order->update_time = order->create_time;

    /* 检查资金/持仓是否足够 */
    double order_value = order->price * order->quantity;
    double commission = order_value * broker->config.commission_rate;

    if (order->direction == ORDER_BUY) {
        /* 检查资金 */
        if (broker->account.cash < order_value + commission) {
            order->status = ORDER_STATUS_REJECTED;
            strcpy(response->error_msg, "Insufficient cash");
            goto order_response;
        }

        /* 冻结资金 */
        broker->account.cash -= (order_value + commission);
        broker->account.frozen_cash += (order_value + commission);
    } else {
        /* 检查持仓 */
        simulate_position_node_t* pos = (simulate_position_node_t*)hash_table_find(
            broker->position_table, request->symbol);

        if (pos == NULL || pos->long_available < order->quantity) {
            order->status = ORDER_STATUS_REJECTED;
            strcpy(response->error_msg, "Insufficient position");
            goto order_response;
        }
    }

    /* 尝试成交（根据成交概率） */
    if (rand() % 100 < broker->config.fill_probability) {
        /* 模拟成交 */
        double slippage = order->price * broker->config.slippage_rate;
        if (order->direction == ORDER_BUY) {
            order->price += slippage;
        } else {
            order->price -= slippage;
        }

        order->filled_quantity = order->quantity;
        order->avg_price = order->price;
        order->status = ORDER_STATUS_FILLED;

        /* 更新持仓 */
        simulate_position_node_t* pos = (simulate_position_node_t*)hash_table_find(
            broker->position_table, request->symbol);

        if (pos == NULL) {
            /* 新建持仓 */
            pos = (simulate_position_node_t*)malloc(sizeof(simulate_position_node_t));
            if (pos != NULL) {
                strncpy(pos->symbol, request->symbol, SYMBOL_MAX_LEN - 1);
                pos->long_position = 0;
                pos->short_position = 0;
                pos->long_available = 0;
                pos->short_available = 0;
                pos->long_cost = 0.0;
                pos->short_cost = 0.0;
                pos->market_value = 0.0;
                pos->next = broker->positions;
                broker->positions = pos;
                broker->position_count++;

                hash_table_insert(broker->position_table, request->symbol, pos);
            }
        }

        if (pos != NULL) {
            if (order->direction == ORDER_BUY) {
                /* 买入：增加多头持仓 */
                double new_cost = pos->long_cost + order->filled_quantity * order->avg_price;
                pos->long_position += order->filled_quantity;
                pos->long_available += order->filled_quantity;
                pos->long_cost = new_cost;
            } else {
                /* 卖出：减少多头持仓 */
                pos->long_position -= order->filled_quantity;
                pos->long_available -= order->filled_quantity;
            }

            /* 更新市值 */
            double current_price = order->avg_price;
            void* price_ptr = hash_table_find(broker->market_data, request->symbol);
            if (price_ptr != NULL) {
                current_price = *(double*)price_ptr;
            }
            pos->market_value = pos->long_position * current_price;
        }

        /* 更新资金 */
        if (order->direction == ORDER_BUY) {
            broker->account.frozen_cash -= (order_value + commission);
        } else {
            broker->account.cash += order_value - commission;
        }

        LOG_INFO("Order %s filled: %s %lld @ %.2f",
                 order_id, order->direction == ORDER_BUY ? "BUY" : "SELL",
                 order->filled_quantity, order->avg_price);
    } else {
        /* 未成交 */
        order->status = ORDER_STATUS_SUBMITTED;

        if (order->direction == ORDER_BUY) {
            /* 释放冻结资金 */
            broker->account.cash += order_value + commission;
            broker->account.frozen_cash -= order_value + commission;
        }

        LOG_INFO("Order %s submitted, waiting for fill", order_id);
    }

order_response:
    /* 构建响应 */
    strncpy(response->order_id, order->order_id, sizeof(response->order_id) - 1);
    strncpy(response->symbol, order->symbol, SYMBOL_MAX_LEN - 1);
    response->direction = order->direction;
    response->type = order->type;
    response->price = order->price;
    response->quantity = order->quantity;
    response->filled_quantity = order->filled_quantity;
    response->filled_price = order->avg_price;
    response->status = order->status;
    response->create_time = order->create_time;
    response->update_time = order->update_time;

    /* 添加到订单链表 */
    order->next = broker->orders;
    broker->orders = order;
    broker->order_count++;

    hash_table_insert(broker->order_table, order->order_id, order);

    pthread_mutex_unlock(&broker->mutex);

    /* 模拟延迟 */
    if (broker->config.latency_ms > 0) {
        usleep(broker->config.latency_ms * 1000);
    }

    return GNTS_OK;
}

/* 撤销订单 */
static int simulate_broker_cancel_order(broker_interface_t* self, const char* order_id) {
    if (self == NULL || order_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    simulate_order_node_t* order = (simulate_order_node_t*)hash_table_find(
        broker->order_table, order_id);

    if (order == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_NOT_FOUND;
    }

    /* 只能撤销未成交的订单 */
    if (order->status == ORDER_STATUS_SUBMITTED) {
        order->status = ORDER_STATUS_CANCELLED;
        order->update_time = get_current_timestamp();

        /* 释放冻结资金 */
        if (order->direction == ORDER_BUY) {
            double order_value = order->price * order->quantity;
            double commission = order_value * broker->config.commission_rate;
            broker->account.cash += order_value + commission;
            broker->account.frozen_cash -= order_value + commission;
        }

        LOG_INFO("Order %s cancelled", order_id);
    }

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询订单 */
static int simulate_broker_query_order(broker_interface_t* self, const char* order_id,
                                        order_response_t* response) {
    if (self == NULL || order_id == NULL || response == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    simulate_order_node_t* order = (simulate_order_node_t*)hash_table_find(
        broker->order_table, order_id);

    if (order == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_NOT_FOUND;
    }

    strncpy(response->order_id, order->order_id, sizeof(response->order_id) - 1);
    strncpy(response->symbol, order->symbol, SYMBOL_MAX_LEN - 1);
    response->direction = order->direction;
    response->type = order->type;
    response->price = order->price;
    response->quantity = order->quantity;
    response->filled_quantity = order->filled_quantity;
    response->filled_price = order->avg_price;
    response->status = order->status;
    response->create_time = order->create_time;
    response->update_time = order->update_time;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询所有订单 */
static int simulate_broker_query_orders(broker_interface_t* self,
                                         order_response_t** orders, int* count) {
    if (self == NULL || orders == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    if (broker->order_count == 0) {
        *orders = NULL;
        *count = 0;
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_OK;
    }

    *orders = (order_response_t*)malloc(sizeof(order_response_t) * broker->order_count);
    if (*orders == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_MEMORY;
    }

    int index = 0;
    simulate_order_node_t* node = broker->orders;
    while (node != NULL) {
        order_response_t* resp = &(*orders)[index];
        strncpy(resp->order_id, node->order_id, sizeof(resp->order_id) - 1);
        strncpy(resp->symbol, node->symbol, SYMBOL_MAX_LEN - 1);
        resp->direction = node->direction;
        resp->type = node->type;
        resp->price = node->price;
        resp->quantity = node->quantity;
        resp->filled_quantity = node->filled_quantity;
        resp->filled_price = node->avg_price;
        resp->status = node->status;
        resp->create_time = node->create_time;
        resp->update_time = node->update_time;

        index++;
        node = node->next;
    }

    *count = broker->order_count;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询成交 */
static int simulate_broker_query_trade(broker_interface_t* self, const char* trade_id,
                                        trade_record_t* trade) {
    if (self == NULL || trade_id == NULL || trade == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    simulate_trade_node_t* node = (simulate_trade_node_t*)hash_table_find(
        broker->trade_table, trade_id);

    if (node == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_NOT_FOUND;
    }

    strncpy(trade->trade_id, node->trade_id, sizeof(trade->trade_id) - 1);
    strncpy(trade->order_id, node->order_id, sizeof(trade->order_id) - 1);
    strncpy(trade->symbol, node->symbol, SYMBOL_MAX_LEN - 1);
    trade->direction = node->direction;
    trade->price = node->price;
    trade->quantity = node->quantity;
    trade->trade_time = node->trade_time;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 查询所有成交 */
static int simulate_broker_query_trades(broker_interface_t* self,
                                         trade_record_t** trades, int* count) {
    if (self == NULL || trades == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    pthread_mutex_lock(&broker->mutex);

    if (broker->trade_count == 0) {
        *trades = NULL;
        *count = 0;
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_OK;
    }

    *trades = (trade_record_t*)malloc(sizeof(trade_record_t) * broker->trade_count);
    if (*trades == NULL) {
        pthread_mutex_unlock(&broker->mutex);
        return GNTS_ERROR_MEMORY;
    }

    int index = 0;
    simulate_trade_node_t* node = broker->trades;
    while (node != NULL) {
        trade_record_t* t = &(*trades)[index];
        strncpy(t->trade_id, node->trade_id, sizeof(t->trade_id) - 1);
        strncpy(t->order_id, node->order_id, sizeof(t->order_id) - 1);
        strncpy(t->symbol, node->symbol, SYMBOL_MAX_LEN - 1);
        t->direction = node->direction;
        t->price = node->price;
        t->quantity = node->quantity;
        t->trade_time = node->trade_time;

        index++;
        node = node->next;
    }

    *count = broker->trade_count;

    pthread_mutex_unlock(&broker->mutex);

    return GNTS_OK;
}

/* 订阅行情 */
static int simulate_broker_subscribe(broker_interface_t* self, const char* symbol) {
    if (self == NULL || symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    LOG_INFO("Simulated subscription to %s", symbol);
    return GNTS_OK;
}

/* 取消订阅 */
static int simulate_broker_unsubscribe(broker_interface_t* self, const char* symbol) {
    if (self == NULL || symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    LOG_INFO("Simulated unsubscription from %s", symbol);
    return GNTS_OK;
}

/* 设置模拟市场数据 */
int simulate_broker_set_market_data(broker_interface_t* self,
                                     const char* symbol, double price) {
    if (self == NULL || symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    simulate_broker_t* broker = (simulate_broker_t*)self->private_data;

    double* price_ptr = (double*)malloc(sizeof(double));
    if (price_ptr == NULL) {
        return GNTS_ERROR_MEMORY;
    }

    *price_ptr = price;

    hash_table_insert(broker->market_data, symbol, price_ptr);

    return GNTS_OK;
}