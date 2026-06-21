/*
 * GNTS - 观澜 · Nexus Trading System
 * 数据管理器实现
 */

#include "data_system/data_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 数据管理器内部结构 */
struct data_manager_impl {
    data_source_interface_t** sources;
    int source_count;
    int max_sources;
    storage_interface_t* storage;
    tick_callback_t tick_callback;
    bar_callback_t bar_callback;
    void* callback_user_data;
    pthread_mutex_t mutex;
    int is_running;
    pthread_t thread;
};

/* 创建数据管理器 */
data_manager_t* data_manager_create(void) {
    struct data_manager_impl* manager = (struct data_manager_impl*)malloc(sizeof(struct data_manager_impl));
    if (manager == NULL) {
        LOG_ERROR("Failed to allocate memory for data manager");
        return NULL;
    }

    manager->sources = (data_source_interface_t**)malloc(sizeof(data_source_interface_t*) * 10);
    if (manager->sources == NULL) {
        LOG_ERROR("Failed to allocate memory for data sources");
        free(manager);
        return NULL;
    }

    manager->source_count = 0;
    manager->max_sources = 10;
    manager->storage = NULL;
    manager->tick_callback = NULL;
    manager->bar_callback = NULL;
    manager->callback_user_data = NULL;
    manager->is_running = 0;
    pthread_mutex_init(&manager->mutex, NULL);

    LOG_INFO("Data manager created successfully");
    return (data_manager_t*)manager;
}

/* 销毁数据管理器 */
void data_manager_destroy(data_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    /* 停止管理器 */
    if (impl->is_running) {
        data_manager_stop(manager);
    }

    /* 断开所有数据源 */
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->disconnect != NULL) {
            impl->sources[i]->disconnect(impl->sources[i]);
        }
    }

    pthread_mutex_destroy(&impl->mutex);
    free(impl->sources);
    free(impl);

    LOG_INFO("Data manager destroyed");
}

/* 添加数据源 */
int data_manager_add_source(data_manager_t* manager, data_source_interface_t* source) {
    if (manager == NULL || source == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    if (impl->source_count >= impl->max_sources) {
        /* 扩容 */
        int new_max = impl->max_sources * 2;
        data_source_interface_t** new_sources = (data_source_interface_t**)realloc(
            impl->sources, sizeof(data_source_interface_t*) * new_max);
        if (new_sources == NULL) {
            pthread_mutex_unlock(&impl->mutex);
            LOG_ERROR("Failed to expand data sources array");
            return GNTS_ERROR_MEMORY;
        }
        impl->sources = new_sources;
        impl->max_sources = new_max;
    }

    impl->sources[impl->source_count++] = source;

    pthread_mutex_unlock(&impl->mutex);
    LOG_INFO("Data source added, count: %d", impl->source_count);
    return GNTS_OK;
}

/* 设置存储 */
int data_manager_set_storage(data_manager_t* manager, storage_interface_t* storage) {
    if (manager == NULL || storage == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;
    impl->storage = storage;

    LOG_INFO("Storage set successfully");
    return GNTS_OK;
}

/* 订阅行情 */
int data_manager_subscribe(data_manager_t* manager, const char* symbol) {
    if (manager == NULL || symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    int result = GNTS_OK;
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->subscribe != NULL) {
            int ret = impl->sources[i]->subscribe(impl->sources[i], symbol);
            if (ret != GNTS_OK) {
                result = ret;
                LOG_WARNING("Failed to subscribe %s on source %d", symbol, i);
            } else {
                LOG_INFO("Subscribed %s on source %d", symbol, i);
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    return result;
}

/* 取消订阅 */
int data_manager_unsubscribe(data_manager_t* manager, const char* symbol) {
    if (manager == NULL || symbol == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    int result = GNTS_OK;
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->unsubscribe != NULL) {
            int ret = impl->sources[i]->unsubscribe(impl->sources[i], symbol);
            if (ret != GNTS_OK) {
                result = ret;
                LOG_WARNING("Failed to unsubscribe %s on source %d", symbol, i);
            } else {
                LOG_INFO("Unsubscribed %s on source %d", symbol, i);
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    return result;
}

/* 查询历史K线 */
int data_manager_query_bars(data_manager_t* manager, const char* symbol,
                            timestamp_t start, timestamp_t end,
                            bar_period_t period, bar_data_t** bars, int* count) {
    if (manager == NULL || symbol == NULL || bars == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    /* 先从存储查询 */
    if (impl->storage != NULL && impl->storage->query_bars != NULL) {
        int ret = impl->storage->query_bars(symbol, start, end, period, bars, count);
        if (ret == GNTS_OK && *count > 0) {
            LOG_INFO("Query %d bars from storage for %s", *count, symbol);
            return GNTS_OK;
        }
    }

    /* 从数据源查询 */
    pthread_mutex_lock(&impl->mutex);

    int result = GNTS_ERROR_NOT_FOUND;
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->query_history != NULL) {
            int ret = impl->sources[i]->query_history(
                impl->sources[i], symbol, start, end, period, bars, count);
            if (ret == GNTS_OK && *count > 0) {
                result = GNTS_OK;
                LOG_INFO("Query %d bars from source %d for %s", *count, i, symbol);

                /* 保存到存储 */
                if (impl->storage != NULL && impl->storage->save_bars != NULL) {
                    impl->storage->save_bars(*bars, *count);
                }
                break;
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    return result;
}

/* 查询历史Tick */
int data_manager_query_ticks(data_manager_t* manager, const char* symbol,
                             timestamp_t start, timestamp_t end,
                             tick_data_t** ticks, int* count) {
    if (manager == NULL || symbol == NULL || ticks == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    /* 先从存储查询 */
    if (impl->storage != NULL && impl->storage->query_ticks != NULL) {
        int ret = impl->storage->query_ticks(symbol, start, end, ticks, count);
        if (ret == GNTS_OK && *count > 0) {
            LOG_INFO("Query %d ticks from storage for %s", *count, symbol);
            return GNTS_OK;
        }
    }

    /* 从数据源查询 */
    pthread_mutex_lock(&impl->mutex);

    int result = GNTS_ERROR_NOT_FOUND;
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->query_tick != NULL) {
            int ret = impl->sources[i]->query_tick(
                impl->sources[i], symbol, start, end, ticks, count);
            if (ret == GNTS_OK && *count > 0) {
                result = GNTS_OK;
                LOG_INFO("Query %d ticks from source %d for %s", *count, i, symbol);

                /* 保存到存储 */
                if (impl->storage != NULL && impl->storage->save_ticks != NULL) {
                    impl->storage->save_ticks(*ticks, *count);
                }
                break;
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    return result;
}

/* 查询财务数据 */
int data_manager_query_financial(data_manager_t* manager, const char* symbol,
                                 financial_data_t* data) {
    if (manager == NULL || symbol == NULL || data == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    int result = GNTS_ERROR_NOT_FOUND;
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->query_financial != NULL) {
            int ret = impl->sources[i]->query_financial(impl->sources[i], symbol, data);
            if (ret == GNTS_OK) {
                result = GNTS_OK;
                LOG_INFO("Query financial data for %s from source %d", symbol, i);
                break;
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    return result;
}

/* 设置行情回调 */
int data_manager_set_callback(data_manager_t* manager,
                              tick_callback_t tick_cb,
                              bar_callback_t bar_cb,
                              void* user_data) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;
    impl->tick_callback = tick_cb;
    impl->bar_callback = bar_cb;
    impl->callback_user_data = user_data;

    return GNTS_OK;
}

/* 数据接收线程函数 */
static void* data_receive_thread(void* arg) {
    struct data_manager_impl* impl = (struct data_manager_impl*)arg;

    LOG_INFO("Data receive thread started");

    while (impl->is_running) {
        /* 这里应该从数据源接收数据并调用回调 */
        /* 实际实现中需要根据数据源类型进行具体处理 */
        usleep(1000); /* 1ms */
    }

    LOG_INFO("Data receive thread stopped");
    return NULL;
}

/* 启动数据管理器 */
int data_manager_start(data_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    if (impl->is_running) {
        LOG_WARNING("Data manager is already running");
        return GNTS_OK;
    }

    /* 连接所有数据源 */
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->connect != NULL) {
            int ret = impl->sources[i]->connect(impl->sources[i]);
            if (ret != GNTS_OK) {
                LOG_ERROR("Failed to connect data source %d", i);
                return ret;
            }
            LOG_INFO("Data source %d connected", i);
        }
    }

    /* 初始化存储 */
    if (impl->storage != NULL && impl->storage->init != NULL) {
        /* 这里需要配置，暂时跳过 */
    }

    impl->is_running = 1;

    /* 启动数据接收线程 */
    if (pthread_create(&impl->thread, NULL, data_receive_thread, impl) != 0) {
        LOG_ERROR("Failed to create data receive thread");
        impl->is_running = 0;
        return GNTS_ERROR;
    }

    LOG_INFO("Data manager started");
    return GNTS_OK;
}

/* 停止数据管理器 */
int data_manager_stop(data_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct data_manager_impl* impl = (struct data_manager_impl*)manager;

    if (!impl->is_running) {
        LOG_WARNING("Data manager is not running");
        return GNTS_OK;
    }

    impl->is_running = 0;

    /* 等待线程结束 */
    pthread_join(impl->thread, NULL);

    /* 断开所有数据源 */
    for (int i = 0; i < impl->source_count; i++) {
        if (impl->sources[i]->disconnect != NULL) {
            impl->sources[i]->disconnect(impl->sources[i]);
            LOG_INFO("Data source %d disconnected", i);
        }
    }

    /* 关闭存储 */
    if (impl->storage != NULL && impl->storage->shutdown != NULL) {
        impl->storage->shutdown();
    }

    LOG_INFO("Data manager stopped");
    return GNTS_OK;
}