/*
 * GNTS - 观澜 · Nexus Trading System
 * 策略引擎实现
 */

#include "strategy/strategy_engine.h"
#include "common/log.h"
#include "common/lockfree_queue.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 事件类型 */
typedef enum {
    ENGINE_EVENT_TICK = 1,
    ENGINE_EVENT_BAR = 2,
    ENGINE_EVENT_ORDER = 3,
    ENGINE_EVENT_TRADE = 4,
    ENGINE_EVENT_STOP = 99,
} engine_event_type_t;

/* 事件数据 */
typedef struct {
    engine_event_type_t type;
    union {
        tick_data_t tick;
        bar_data_t bar;
        order_response_t order;
        trade_record_t trade;
    } data;
} engine_event_t;

/* 策略引擎内部结构 */
struct strategy_engine_impl {
    strategy_interface_t** strategies;
    int strategy_count;
    int max_strategies;
    strategy_context_t context;
    pthread_mutex_t mutex;
    int is_running;
    pthread_t thread;
    void* data_manager;             /* 数据管理器引用 */
    void* order_manager;            /* 订单管理器引用 */
    lockfree_queue_t* event_queue;  /* 事件队列 */
};

/* 创建策略引擎 */
strategy_engine_t* strategy_engine_create(void) {
    struct strategy_engine_impl* engine = (struct strategy_engine_impl*)malloc(sizeof(struct strategy_engine_impl));
    if (engine == NULL) {
        LOG_ERROR("Failed to allocate memory for strategy engine");
        return NULL;
    }

    engine->strategies = (strategy_interface_t**)malloc(sizeof(strategy_interface_t*) * 10);
    if (engine->strategies == NULL) {
        LOG_ERROR("Failed to allocate memory for strategies");
        free(engine);
        return NULL;
    }

    engine->strategy_count = 0;
    engine->max_strategies = 10;
    engine->is_running = 0;
    engine->data_manager = NULL;
    engine->order_manager = NULL;
    pthread_mutex_init(&engine->mutex, NULL);

    /* 创建事件队列 */
    engine->event_queue = lockfree_queue_create(10000);
    if (engine->event_queue == NULL) {
        LOG_ERROR("Failed to create event queue");
        free(engine->strategies);
        free(engine);
        return NULL;
    }

    LOG_INFO("Strategy engine created");
    return (strategy_engine_t*)engine;
}

/* 销毁策略引擎 */
void strategy_engine_destroy(strategy_engine_t* engine) {
    if (engine == NULL) {
        return;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    /* 停止引擎 */
    if (impl->is_running) {
        strategy_engine_stop(engine);
    }

    pthread_mutex_destroy(&impl->mutex);

    /* 释放事件队列中的所有事件 */
    engine_event_t* event;
    while ((event = (engine_event_t*)lockfree_queue_pop(impl->event_queue)) != NULL) {
        free(event);
    }
    lockfree_queue_destroy(impl->event_queue);

    free(impl->strategies);
    free(impl);

    LOG_INFO("Strategy engine destroyed");
}

/* 设置数据管理器 */
int strategy_engine_set_data_manager(strategy_engine_t* engine, void* data_manager) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);
    impl->data_manager = data_manager;
    impl->context.data_manager = data_manager;
    pthread_mutex_unlock(&impl->mutex);

    LOG_INFO("Data manager set for strategy engine");
    return GNTS_OK;
}

/* 设置订单管理器 */
int strategy_engine_set_order_manager(strategy_engine_t* engine, void* order_manager) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);
    impl->order_manager = order_manager;
    impl->context.order_manager = order_manager;
    pthread_mutex_unlock(&impl->mutex);

    LOG_INFO("Order manager set for strategy engine");
    return GNTS_OK;
}

/* 喂入K线数据 */
int strategy_engine_feed_bar(strategy_engine_t* engine, const bar_data_t* bar) {
    if (engine == NULL || bar == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    engine_event_t* event = (engine_event_t*)malloc(sizeof(engine_event_t));
    if (event == NULL) {
        LOG_ERROR("Failed to allocate bar event");
        return GNTS_ERROR_MEMORY;
    }

    event->type = ENGINE_EVENT_BAR;
    event->data.bar = *bar;

    if (lockfree_queue_push(impl->event_queue, event) != 0) {
        LOG_WARNING("Event queue full, dropping bar event for %s", bar->symbol);
        free(event);
        return GNTS_ERROR;
    }

    return GNTS_OK;
}

/* 喂入Tick数据 */
int strategy_engine_feed_tick(strategy_engine_t* engine, const tick_data_t* tick) {
    if (engine == NULL || tick == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    engine_event_t* event = (engine_event_t*)malloc(sizeof(engine_event_t));
    if (event == NULL) {
        LOG_ERROR("Failed to allocate tick event");
        return GNTS_ERROR_MEMORY;
    }

    event->type = ENGINE_EVENT_TICK;
    event->data.tick = *tick;

    if (lockfree_queue_push(impl->event_queue, event) != 0) {
        LOG_WARNING("Event queue full, dropping tick event for %s", tick->symbol);
        free(event);
        return GNTS_ERROR;
    }

    return GNTS_OK;
}

/* 添加策略 */
int strategy_engine_add_strategy(strategy_engine_t* engine, strategy_interface_t* strategy) {
    if (engine == NULL || strategy == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    if (impl->strategy_count >= impl->max_strategies) {
        /* 扩容 */
        int new_max = impl->max_strategies * 2;
        strategy_interface_t** new_strategies = (strategy_interface_t**)realloc(
            impl->strategies, sizeof(strategy_interface_t*) * new_max);
        if (new_strategies == NULL) {
            pthread_mutex_unlock(&impl->mutex);
            LOG_ERROR("Failed to expand strategies array");
            return GNTS_ERROR_MEMORY;
        }
        impl->strategies = new_strategies;
        impl->max_strategies = new_max;
    }

    impl->strategies[impl->strategy_count++] = strategy;

    pthread_mutex_unlock(&impl->mutex);
    LOG_INFO("Strategy %s added, count: %d", strategy->name, impl->strategy_count);
    return GNTS_OK;
}

/* 移除策略 */
int strategy_engine_remove_strategy(strategy_engine_t* engine, const char* strategy_id) {
    if (engine == NULL || strategy_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    int found = -1;
    for (int i = 0; i < impl->strategy_count; i++) {
        if (strcmp(impl->strategies[i]->id, strategy_id) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        /* 停止策略 */
        if (impl->strategies[found]->status == STRATEGY_STATUS_RUNNING) {
            if (impl->strategies[found]->on_stop != NULL) {
                impl->strategies[found]->on_stop(impl->strategies[found]);
            }
        }

        /* 移除策略 */
        for (int i = found; i < impl->strategy_count - 1; i++) {
            impl->strategies[i] = impl->strategies[i + 1];
        }
        impl->strategy_count--;

        LOG_INFO("Strategy %s removed", strategy_id);
    }

    pthread_mutex_unlock(&impl->mutex);
    return found >= 0 ? GNTS_OK : GNTS_ERROR_NOT_FOUND;
}

/* 启动策略 */
int strategy_engine_start_strategy(strategy_engine_t* engine, const char* strategy_id) {
    if (engine == NULL || strategy_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->strategy_count; i++) {
        if (strcmp(impl->strategies[i]->id, strategy_id) == 0) {
            if (impl->strategies[i]->status == STRATEGY_STATUS_RUNNING) {
                pthread_mutex_unlock(&impl->mutex);
                LOG_WARNING("Strategy %s is already running", strategy_id);
                return GNTS_OK;
            }

            /* 初始化策略 */
            if (impl->strategies[i]->on_init != NULL) {
                int ret = impl->strategies[i]->on_init(impl->strategies[i], &impl->context);
                if (ret != GNTS_OK) {
                    pthread_mutex_unlock(&impl->mutex);
                    LOG_ERROR("Failed to initialize strategy %s", strategy_id);
                    return ret;
                }
            }

            /* 启动策略 */
            if (impl->strategies[i]->on_start != NULL) {
                impl->strategies[i]->on_start(impl->strategies[i]);
            }

            impl->strategies[i]->status = STRATEGY_STATUS_RUNNING;

            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Strategy %s started", strategy_id);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Strategy %s not found", strategy_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 停止策略 */
int strategy_engine_stop_strategy(strategy_engine_t* engine, const char* strategy_id) {
    if (engine == NULL || strategy_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->strategy_count; i++) {
        if (strcmp(impl->strategies[i]->id, strategy_id) == 0) {
            if (impl->strategies[i]->status != STRATEGY_STATUS_RUNNING) {
                pthread_mutex_unlock(&impl->mutex);
                LOG_WARNING("Strategy %s is not running", strategy_id);
                return GNTS_OK;
            }

            /* 停止策略 */
            if (impl->strategies[i]->on_stop != NULL) {
                impl->strategies[i]->on_stop(impl->strategies[i]);
            }

            impl->strategies[i]->status = STRATEGY_STATUS_STOPPED;

            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Strategy %s stopped", strategy_id);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Strategy %s not found", strategy_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 暂停策略 */
int strategy_engine_pause_strategy(strategy_engine_t* engine, const char* strategy_id) {
    if (engine == NULL || strategy_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->strategy_count; i++) {
        if (strcmp(impl->strategies[i]->id, strategy_id) == 0) {
            if (impl->strategies[i]->status != STRATEGY_STATUS_RUNNING) {
                pthread_mutex_unlock(&impl->mutex);
                LOG_WARNING("Strategy %s is not running", strategy_id);
                return GNTS_OK;
            }

            /* 暂停策略 */
            if (impl->strategies[i]->on_pause != NULL) {
                impl->strategies[i]->on_pause(impl->strategies[i]);
            }

            impl->strategies[i]->status = STRATEGY_STATUS_PAUSED;

            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Strategy %s paused", strategy_id);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Strategy %s not found", strategy_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 恢复策略 */
int strategy_engine_resume_strategy(strategy_engine_t* engine, const char* strategy_id) {
    if (engine == NULL || strategy_id == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->strategy_count; i++) {
        if (strcmp(impl->strategies[i]->id, strategy_id) == 0) {
            if (impl->strategies[i]->status != STRATEGY_STATUS_PAUSED) {
                pthread_mutex_unlock(&impl->mutex);
                LOG_WARNING("Strategy %s is not paused", strategy_id);
                return GNTS_OK;
            }

            /* 恢复策略 */
            if (impl->strategies[i]->on_resume != NULL) {
                impl->strategies[i]->on_resume(impl->strategies[i]);
            }

            impl->strategies[i]->status = STRATEGY_STATUS_RUNNING;

            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Strategy %s resumed", strategy_id);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Strategy %s not found", strategy_id);
    return GNTS_ERROR_NOT_FOUND;
}

/* 分发事件到所有运行中的策略 */
static void dispatch_event(struct strategy_engine_impl* impl, engine_event_t* event) {
    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->strategy_count; i++) {
        if (impl->strategies[i]->status != STRATEGY_STATUS_RUNNING) {
            continue;
        }

        switch (event->type) {
            case ENGINE_EVENT_TICK:
                if (impl->strategies[i]->on_tick != NULL) {
                    impl->strategies[i]->on_tick(impl->strategies[i], &event->data.tick);
                }
                break;
            case ENGINE_EVENT_BAR:
                if (impl->strategies[i]->on_bar != NULL) {
                    impl->strategies[i]->on_bar(impl->strategies[i], &event->data.bar);
                }
                break;
            case ENGINE_EVENT_ORDER:
                if (impl->strategies[i]->on_order != NULL) {
                    impl->strategies[i]->on_order(impl->strategies[i], &event->data.order);
                }
                break;
            case ENGINE_EVENT_TRADE:
                if (impl->strategies[i]->on_trade != NULL) {
                    impl->strategies[i]->on_trade(impl->strategies[i], &event->data.trade);
                }
                break;
            default:
                break;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
}

/* 策略引擎线程函数 */
static void* strategy_engine_thread(void* arg) {
    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)arg;

    LOG_INFO("Strategy engine event loop started");

    while (impl->is_running) {
        /* 从事件队列中获取事件 */
        engine_event_t* event = (engine_event_t*)lockfree_queue_pop(impl->event_queue);

        if (event != NULL) {
            if (event->type == ENGINE_EVENT_STOP) {
                free(event);
                break;
            }

            /* 分发事件到所有策略 */
            dispatch_event(impl, event);

            free(event);
        } else {
            /* 队列为空，短暂休眠避免忙等 */
            usleep(100); /* 0.1ms */
        }
    }

    LOG_INFO("Strategy engine event loop stopped");
    return NULL;
}

/* 启动引擎 */
int strategy_engine_start(strategy_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    if (impl->is_running) {
        LOG_WARNING("Strategy engine is already running");
        return GNTS_OK;
    }

    impl->is_running = 1;

    /* 启动引擎线程 */
    if (pthread_create(&impl->thread, NULL, strategy_engine_thread, impl) != 0) {
        LOG_ERROR("Failed to create strategy engine thread");
        impl->is_running = 0;
        return GNTS_ERROR;
    }

    LOG_INFO("Strategy engine started");
    return GNTS_OK;
}

/* 停止引擎 */
int strategy_engine_stop(strategy_engine_t* engine) {
    if (engine == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct strategy_engine_impl* impl = (struct strategy_engine_impl*)engine;

    if (!impl->is_running) {
        LOG_WARNING("Strategy engine is not running");
        return GNTS_OK;
    }

    impl->is_running = 0;

    /* 发送停止事件唤醒线程 */
    engine_event_t* stop_event = (engine_event_t*)malloc(sizeof(engine_event_t));
    if (stop_event != NULL) {
        stop_event->type = ENGINE_EVENT_STOP;
        lockfree_queue_push(impl->event_queue, stop_event);
    }

    /* 停止所有策略 */
    pthread_mutex_lock(&impl->mutex);
    for (int i = 0; i < impl->strategy_count; i++) {
        if (impl->strategies[i]->status == STRATEGY_STATUS_RUNNING) {
            if (impl->strategies[i]->on_stop != NULL) {
                impl->strategies[i]->on_stop(impl->strategies[i]);
            }
            impl->strategies[i]->status = STRATEGY_STATUS_STOPPED;
        }
    }
    pthread_mutex_unlock(&impl->mutex);

    /* 等待线程结束 */
    pthread_join(impl->thread, NULL);

    LOG_INFO("Strategy engine stopped");
    return GNTS_OK;
}