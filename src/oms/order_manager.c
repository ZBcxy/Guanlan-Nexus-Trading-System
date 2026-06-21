/*
 * GNTS - 观澜 · Nexus Trading System
 * 订单管理器实现
 */

#include "oms/order_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 订单管理器内部结构 */
struct order_manager_impl {
    broker_interface_t* broker;
    order_response_t* orders;
    int order_count;
    int max_orders;
    trade_record_t* trades;
    int trade_count;
    int max_trades;
    order_callback_t order_callback;
    trade_callback_t trade_callback;
    void* callback_user_data;
    pthread_mutex_t mutex;
    int is_running;
    pthread_t thread;
};

/* 创建订单管理器 */
order_manager_t* order_manager_create(void) {
    struct order_manager_impl* manager = (struct order_manager_impl*)malloc(sizeof(struct order_manager_impl));
    if (manager == NULL) {
        LOG_ERROR("Failed to allocate memory for order manager");
        return NULL;
    }

    manager->broker = NULL;
    manager->orders = NULL;
    manager->order_count = 0;
    manager->max_orders = 0;
    manager->trades = NULL;
    manager->trade_count = 0;
    manager->max_trades = 0;
    manager->order_callback = NULL;
    manager->trade_callback = NULL;
    manager->callback_user_data = NULL;
    manager->is_running = 0;
    pthread_mutex_init(&manager->mutex, NULL);

    LOG_INFO("Order manager created");
    return (order_manager_t*)manager;
}

/* 销毁订单管理器 */
void order_manager_destroy(order_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    /* 停止管理器 */
    if (impl->is_running) {
        order_manager_stop(manager);
    }

    /* 断开券商 */
    if (impl->broker != NULL && impl->broker->disconnect != NULL) {
        impl->broker->disconnect(impl->broker);
    }

    pthread_mutex_destroy(&impl->mutex);

    if (impl->orders != NULL) {
        free(impl->orders);
    }
    if (impl->trades != NULL) {
        free(impl->trades);
    }

    free(impl);
    LOG_INFO("Order manager destroyed");
}

/* 设置券商接口 */
int order_manager_set_broker(order_manager_t* manager, broker_interface_t* broker) {
    if (manager == NULL || broker == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;
    impl->broker = broker;

    LOG_INFO("Broker interface set");
    return GNTS_OK;
}

/* 连接券商 */
int order_manager_connect(order_manager_t* manager, const broker_config_t* config) {
    if (manager == NULL || config == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (impl->broker == NULL) {
        LOG_ERROR("No broker interface set");
        return GNTS_ERROR;
    }

    if (impl->broker->connect == NULL) {
        LOG_ERROR("Broker interface has no connect function");
        return GNTS_ERROR;
    }

    int ret = impl->broker->connect(impl->broker, config);
    if (ret != GNTS_OK) {
        LOG_ERROR("Failed to connect broker");
        return ret;
    }

    LOG_INFO("Broker connected");
    return GNTS_OK;
}

/* 断开券商 */
int order_manager_disconnect(order_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (impl->broker == NULL) {
        LOG_WARNING("No broker interface set");
        return GNTS_OK;
    }

    if (impl->broker->disconnect != NULL) {
        impl->broker->disconnect(impl->broker);
    }

    LOG_INFO("Broker disconnected");
    return GNTS_OK;
}

/* 发送订单 */
int order_manager_send_order(order_manager_t* manager, const order_request_t* request,
                             order_response_t* response) {
    if (manager == NULL || request == NULL || response == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (impl->broker == NULL) {
        LOG_ERROR("No broker interface set");
        return GNTS_ERROR;
    }

    if (impl->broker->send_order == NULL) {
        LOG_ERROR("Broker interface has no send_order function");
        return GNTS_ERROR;
    }

    pthread_mutex_lock(&impl->mutex);

    int ret = impl->broker->send_order(impl->broker, request, response);
    if (ret != GNTS_OK) {
        pthread_mutex_unlock(&impl->mutex);
        LOG_ERROR("Failed to send order");
        return ret;
    }

    /* 保存订单（动态扩容） */
    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->orders, impl->order_count, impl->max_orders,
                                  sizeof(order_response_t), 256);

    impl->orders[impl->order_count++] = *response;

    pthread_mutex_unlock(&impl->mutex);

    /* 调用回调 */
    if (impl->order_callback != NULL) {
        impl->order_callback(response, impl->callback_user_data);
    }

    LOG_INFO("Order sent: %s", request->order_id);
    return GNTS_OK;
}

/* 撤销订单 */
int order_manager_cancel_order(order_manager_t* manager, const char* order_id) {
    if (manager == NULL || order_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (impl->broker == NULL) {
        LOG_ERROR("No broker interface set");
        return GNTS_ERROR;
    }

    if (impl->broker->cancel_order == NULL) {
        LOG_ERROR("Broker interface has no cancel_order function");
        return GNTS_ERROR;
    }

    int ret = impl->broker->cancel_order(impl->broker, order_id);
    if (ret != GNTS_OK) {
        LOG_ERROR("Failed to cancel order %s", order_id);
        return ret;
    }

    LOG_INFO("Order cancelled: %s", order_id);
    return GNTS_OK;
}

/* 查询订单 */
int order_manager_query_order(order_manager_t* manager, const char* order_id,
                              order_response_t* response) {
    if (manager == NULL || order_id == NULL || response == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    /* 先从本地查找 */
    for (int i = 0; i < impl->order_count; i++) {
        if (strcmp(impl->orders[i].order_id, order_id) == 0) {
            *response = impl->orders[i];
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    /* 从券商查询 */
    if (impl->broker != NULL && impl->broker->query_order != NULL) {
        int ret = impl->broker->query_order(impl->broker, order_id, response);
        if (ret == GNTS_OK) {
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Order %s not found", order_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 查询所有订单 */
int order_manager_query_orders(order_manager_t* manager, order_response_t** orders, int* count) {
    if (manager == NULL || orders == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);
    *orders = impl->orders;
    *count = impl->order_count;
    pthread_mutex_unlock(&impl->mutex);

    return GNTS_OK;
}

/* 查询成交 */
int order_manager_query_trade(order_manager_t* manager, const char* trade_id,
                              trade_record_t* trade) {
    if (manager == NULL || trade_id == NULL || trade == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    /* 从本地查找 */
    for (int i = 0; i < impl->trade_count; i++) {
        if (strcmp(impl->trades[i].trade_id, trade_id) == 0) {
            *trade = impl->trades[i];
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    /* 从券商查询 */
    if (impl->broker != NULL && impl->broker->query_trade != NULL) {
        int ret = impl->broker->query_trade(impl->broker, trade_id, trade);
        if (ret == GNTS_OK) {
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Trade %s not found", trade_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 查询所有成交 */
int order_manager_query_trades(order_manager_t* manager, trade_record_t** trades, int* count) {
    if (manager == NULL || trades == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);
    *trades = impl->trades;
    *count = impl->trade_count;
    pthread_mutex_unlock(&impl->mutex);

    return GNTS_OK;
}

/* 设置回调 */
int order_manager_set_callback(order_manager_t* manager,
                                order_callback_t order_cb,
                                trade_callback_t trade_cb,
                                void* user_data) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;
    impl->order_callback = order_cb;
    impl->trade_callback = trade_cb;
    impl->callback_user_data = user_data;

    return GNTS_OK;
}

/* 订单接收线程函数 */
static void* order_receive_thread(void* arg) {
    struct order_manager_impl* impl = (struct order_manager_impl*)arg;

    LOG_INFO("Order receive thread started");

    while (impl->is_running) {
        /* 这里应该从券商接收订单回报和成交回报 */
        /* 实际实现中需要根据券商API进行具体处理 */
        usleep(1000); /* 1ms */
    }

    LOG_INFO("Order receive thread stopped");
    return NULL;
}

/* 启动订单管理器 */
int order_manager_start(order_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (impl->is_running) {
        LOG_WARNING("Order manager is already running");
        return GNTS_OK;
    }

    impl->is_running = 1;

    /* 启动订单接收线程 */
    if (pthread_create(&impl->thread, NULL, order_receive_thread, impl) != 0) {
        LOG_ERROR("Failed to create order receive thread");
        impl->is_running = 0;
        return GNTS_ERROR;
    }

    LOG_INFO("Order manager started");
    return GNTS_OK;
}

/* 停止订单管理器 */
int order_manager_stop(order_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct order_manager_impl* impl = (struct order_manager_impl*)manager;

    if (!impl->is_running) {
        LOG_WARNING("Order manager is not running");
        return GNTS_OK;
    }

    impl->is_running = 0;

    /* 等待线程结束 */
    pthread_join(impl->thread, NULL);

    LOG_INFO("Order manager stopped");
    return GNTS_OK;
}