/*
 * GNTS - 观澜 · Nexus Trading System
 * 算法交易实现
 */

#include "oms/order_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* 算法交易器内部结构 */
struct algo_trader_impl {
    algo_config_t config;
    order_manager_t* order_manager;
    int64_t filled_quantity;
    double filled_value;
    int slice_filled;
    int is_running;
    pthread_t thread;
    pthread_mutex_t mutex;
};

/* 创建算法交易器 */
algo_trader_t* algo_trader_create(const algo_config_t* config) {
    if (config == NULL) {
        LOG_ERROR("Invalid config for algo trader");
        return NULL;
    }

    struct algo_trader_impl* trader = (struct algo_trader_impl*)malloc(sizeof(struct algo_trader_impl));
    if (trader == NULL) {
        LOG_ERROR("Failed to allocate memory for algo trader");
        return NULL;
    }

    trader->config = *config;
    trader->order_manager = NULL;
    trader->filled_quantity = 0;
    trader->filled_value = 0.0;
    trader->slice_filled = 0;
    trader->is_running = 0;
    pthread_mutex_init(&trader->mutex, NULL);

    LOG_INFO("Algo trader created for %s", config->symbol);
    return (algo_trader_t*)trader;
}

/* 销毁算法交易器 */
void algo_trader_destroy(algo_trader_t* trader) {
    if (trader == NULL) {
        return;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;

    /* 停止交易器 */
    if (impl->is_running) {
        algo_trader_stop(trader);
    }

    pthread_mutex_destroy(&impl->mutex);
    free(impl);
    LOG_INFO("Algo trader destroyed");
}

/* 设置订单管理器 */
int algo_trader_set_order_manager(algo_trader_t* trader, order_manager_t* manager) {
    if (trader == NULL || manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;
    impl->order_manager = manager;

    LOG_INFO("Order manager set for algo trader");
    return GNTS_OK;
}

/* TWAP算法线程 */
static void* algo_twap_thread(void* arg) {
    struct algo_trader_impl* impl = (struct algo_trader_impl*)arg;

    LOG_INFO("TWAP algo thread started for %s", impl->config.symbol);

    /* 计算每个切片的数量和时间 */
    int64_t slice_quantity = impl->config.total_quantity / impl->config.slice_count;
    double slice_interval = impl->config.slice_interval;

    for (int i = 0; i < impl->config.slice_count && impl->is_running; i++) {
        /* 创建订单 */
        order_request_t request;
        snprintf(request.order_id, sizeof(request.order_id), "TWAP_%d_%d", impl->slice_filled, i);
        strncpy(request.symbol, impl->config.symbol, SYMBOL_MAX_LEN - 1);
        request.direction = impl->config.direction;
        request.type = ORDER_TYPE_LIMIT;
        request.price = impl->config.price_limit;
        request.quantity = slice_quantity;
        request.create_time = get_current_timestamp();

        /* 发送订单 */
        if (impl->order_manager != NULL) {
            order_response_t response;
            int ret = order_manager_send_order(impl->order_manager, &request, &response);
            if (ret == GNTS_OK && response.status == ORDER_STATUS_FILLED) {
                pthread_mutex_lock(&impl->mutex);
                impl->filled_quantity += response.filled_quantity;
                impl->filled_value += response.filled_price * response.filled_quantity;
                impl->slice_filled++;
                pthread_mutex_unlock(&impl->mutex);
            }
        }

        /* 等待下一个切片 */
        usleep((int)(slice_interval * 1000000));
    }

    LOG_INFO("TWAP algo thread stopped, filled: %lld/%lld",
             impl->filled_quantity, impl->config.total_quantity);

    impl->is_running = 0;
    return NULL;
}

/* VWAP算法线程 */
static void* algo_vwap_thread(void* arg) {
    struct algo_trader_impl* impl = (struct algo_trader_impl*)arg;

    LOG_INFO("VWAP algo thread started for %s", impl->config.symbol);

    /* VWAP算法需要根据市场成交量动态调整下单量 */
    /* 这里简化实现，实际需要接入实时行情数据 */

    for (int i = 0; i < impl->config.slice_count && impl->is_running; i++) {
        /* 根据参与率计算下单量 */
        int64_t slice_quantity = impl->config.total_quantity * impl->config.participation_rate;

        /* 创建订单 */
        order_request_t request;
        snprintf(request.order_id, sizeof(request.order_id), "VWAP_%d_%d", impl->slice_filled, i);
        strncpy(request.symbol, impl->config.symbol, SYMBOL_MAX_LEN - 1);
        request.direction = impl->config.direction;
        request.type = ORDER_TYPE_LIMIT;
        request.price = impl->config.price_limit;
        request.quantity = slice_quantity;
        request.create_time = get_current_timestamp();

        /* 发送订单 */
        if (impl->order_manager != NULL) {
            order_response_t response;
            int ret = order_manager_send_order(impl->order_manager, &request, &response);
            if (ret == GNTS_OK && response.status == ORDER_STATUS_FILLED) {
                pthread_mutex_lock(&impl->mutex);
                impl->filled_quantity += response.filled_quantity;
                impl->filled_value += response.filled_price * response.filled_quantity;
                impl->slice_filled++;
                pthread_mutex_unlock(&impl->mutex);
            }
        }

        /* 等待下一个切片 */
        usleep((int)(impl->config.slice_interval * 1000000));
    }

    LOG_INFO("VWAP algo thread stopped, filled: %lld/%lld",
             impl->filled_quantity, impl->config.total_quantity);

    impl->is_running = 0;
    return NULL;
}

/* 冰山指令线程 */
static void* algo_iceberg_thread(void* arg) {
    struct algo_trader_impl* impl = (struct algo_trader_impl*)arg;

    LOG_INFO("Iceberg algo thread started for %s", impl->config.symbol);

    int64_t remaining = impl->config.total_quantity;

    while (remaining > 0 && impl->is_running) {
        /* 计算显示数量 */
        int64_t display_quantity = impl->config.display_quantity;
        if (display_quantity > remaining) {
            display_quantity = remaining;
        }

        /* 创建订单 */
        order_request_t request;
        snprintf(request.order_id, sizeof(request.order_id), "ICE_%d", impl->slice_filled);
        strncpy(request.symbol, impl->config.symbol, SYMBOL_MAX_LEN - 1);
        request.direction = impl->config.direction;
        request.type = ORDER_TYPE_LIMIT;
        request.price = impl->config.price_limit;
        request.quantity = display_quantity;
        request.create_time = get_current_timestamp();

        /* 发送订单 */
        if (impl->order_manager != NULL) {
            order_response_t response;
            int ret = order_manager_send_order(impl->order_manager, &request, &response);
            if (ret == GNTS_OK && response.status == ORDER_STATUS_FILLED) {
                pthread_mutex_lock(&impl->mutex);
                impl->filled_quantity += response.filled_quantity;
                impl->filled_value += response.filled_price * response.filled_quantity;
                impl->slice_filled++;
                remaining -= response.filled_quantity;
                pthread_mutex_unlock(&impl->mutex);
            }
        }

        /* 等待一段时间 */
        usleep(100000); /* 100ms */
    }

    LOG_INFO("Iceberg algo thread stopped, filled: %lld/%lld",
             impl->filled_quantity, impl->config.total_quantity);

    impl->is_running = 0;
    return NULL;
}

/* 启动算法交易 */
int algo_trader_start(algo_trader_t* trader) {
    if (trader == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;

    if (impl->is_running) {
        LOG_WARNING("Algo trader is already running");
        return GNTS_OK;
    }

    if (impl->order_manager == NULL) {
        LOG_ERROR("No order manager set for algo trader");
        return GNTS_ERROR;
    }

    impl->is_running = 1;

    /* 根据算法类型启动线程 */
    void* (*thread_func)(void*) = NULL;

    switch (impl->config.type) {
        case ALGO_TYPE_TWAP:
            thread_func = algo_twap_thread;
            break;
        case ALGO_TYPE_VWAP:
            thread_func = algo_vwap_thread;
            break;
        case ALGO_TYPE_ICEBERG:
            thread_func = algo_iceberg_thread;
            break;
        default:
            LOG_ERROR("Unknown algo type: %d", impl->config.type);
            impl->is_running = 0;
            return GNTS_ERROR_INVALID_PARAM;
    }

    if (pthread_create(&impl->thread, NULL, thread_func, impl) != 0) {
        LOG_ERROR("Failed to create algo thread");
        impl->is_running = 0;
        return GNTS_ERROR;
    }

    LOG_INFO("Algo trader started for %s", impl->config.symbol);
    return GNTS_OK;
}

/* 停止算法交易 */
int algo_trader_stop(algo_trader_t* trader) {
    if (trader == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;

    if (!impl->is_running) {
        LOG_WARNING("Algo trader is not running");
        return GNTS_OK;
    }

    impl->is_running = 0;

    /* 等待线程结束 */
    pthread_join(impl->thread, NULL);

    LOG_INFO("Algo trader stopped for %s", impl->config.symbol);
    return GNTS_OK;
}

/* 获取执行进度 */
double algo_trader_get_progress(algo_trader_t* trader) {
    if (trader == NULL) {
        return 0.0;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;

    pthread_mutex_lock(&impl->mutex);
    double progress = (double)impl->filled_quantity / impl->config.total_quantity;
    pthread_mutex_unlock(&impl->mutex);

    return progress;
}

/* 获取平均成交价 */
double algo_trader_get_avg_price(algo_trader_t* trader) {
    if (trader == NULL) {
        return 0.0;
    }

    struct algo_trader_impl* impl = (struct algo_trader_impl*)trader;

    pthread_mutex_lock(&impl->mutex);

    double avg_price = 0.0;
    if (impl->filled_quantity > 0) {
        avg_price = impl->filled_value / impl->filled_quantity;
    }

    pthread_mutex_unlock(&impl->mutex);

    return avg_price;
}