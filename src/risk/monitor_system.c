/*
 * GNTS - 观澜 · Nexus Trading System
 * 监控系统实现
 */

#include "risk/risk_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 监控系统内部结构 */
struct monitor_system_impl {
    metric_t* metrics;
    int metric_count;
    int max_metrics;
    alert_t* alerts;
    int alert_count;
    int max_alerts;
    alert_callback_t alert_callback;
    void* callback_user_data;
    int is_running;
    pthread_t thread;
    pthread_mutex_t mutex;
};

/* 创建监控系统 */
monitor_system_t* monitor_system_create(void) {
    struct monitor_system_impl* monitor = (struct monitor_system_impl*)malloc(sizeof(struct monitor_system_impl));
    if (monitor == NULL) {
        LOG_ERROR("Failed to allocate memory for monitor system");
        return NULL;
    }

    monitor->metrics = NULL;
    monitor->metric_count = 0;
    monitor->max_metrics = 0;
    monitor->alerts = NULL;
    monitor->alert_count = 0;
    monitor->max_alerts = 0;
    monitor->alert_callback = NULL;
    monitor->callback_user_data = NULL;
    monitor->is_running = 0;
    pthread_mutex_init(&monitor->mutex, NULL);

    LOG_INFO("Monitor system created");
    return (monitor_system_t*)monitor;
}

/* 销毁监控系统 */
void monitor_system_destroy(monitor_system_t* monitor) {
    if (monitor == NULL) {
        return;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    /* 停止系统 */
    if (impl->is_running) {
        monitor_system_stop(monitor);
    }

    pthread_mutex_destroy(&impl->mutex);

    if (impl->metrics != NULL) {
        free(impl->metrics);
    }
    if (impl->alerts != NULL) {
        free(impl->alerts);
    }

    free(impl);
    LOG_INFO("Monitor system destroyed");
}

/* 注册指标 */
int monitor_system_register_metric(monitor_system_t* monitor, const char* name,
                                    metric_type_t type, const char* description) {
    if (monitor == NULL || name == NULL || description == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    /* 检查是否已存在 */
    for (int i = 0; i < impl->metric_count; i++) {
        if (strcmp(impl->metrics[i].name, name) == 0) {
            pthread_mutex_unlock(&impl->mutex);
            LOG_WARNING("Metric %s already registered", name);
            return GNTS_OK;
        }
    }

    /* 添加新指标（动态扩容） */
    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->metrics, impl->metric_count, impl->max_metrics,
                                  sizeof(metric_t), 32);

    metric_t* metric = &impl->metrics[impl->metric_count++];
    strncpy(metric->name, name, STRING_MAX_LEN - 1);
    metric->type = type;
    strncpy(metric->description, description, STRING_MAX_LEN - 1);
    metric->value = 0.0;
    metric->timestamp = 0;
    metric->label_count = 0;

    pthread_mutex_unlock(&impl->mutex);
    LOG_INFO("Metric %s registered", name);
    return GNTS_OK;
}

/* 更新指标 */
int monitor_system_update_metric(monitor_system_t* monitor, const char* name, double value) {
    if (monitor == NULL || name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->metric_count; i++) {
        if (strcmp(impl->metrics[i].name, name) == 0) {
            impl->metrics[i].value = value;
            impl->metrics[i].timestamp = get_current_timestamp();
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Metric %s not found", name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 增加指标（计数器） */
int monitor_system_increment_metric(monitor_system_t* monitor, const char* name, double delta) {
    if (monitor == NULL || name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->metric_count; i++) {
        if (strcmp(impl->metrics[i].name, name) == 0) {
            impl->metrics[i].value += delta;
            impl->metrics[i].timestamp = get_current_timestamp();
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Metric %s not found", name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 查询指标 */
int monitor_system_query_metric(monitor_system_t* monitor, const char* name, metric_t* metric) {
    if (monitor == NULL || name == NULL || metric == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->metric_count; i++) {
        if (strcmp(impl->metrics[i].name, name) == 0) {
            *metric = impl->metrics[i];
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Metric %s not found", name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 触发告警 */
int monitor_system_trigger_alert(monitor_system_t* monitor, const char* name,
                                 alert_level_t level, const char* message) {
    if (monitor == NULL || name == NULL || message == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    /* 添加告警（动态扩容） */
    DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->alerts, impl->alert_count, impl->max_alerts,
                                  sizeof(alert_t), 32);

    alert_t* alert = &impl->alerts[impl->alert_count++];
    strncpy(alert->name, name, STRING_MAX_LEN - 1);
    alert->level = level;
    strncpy(alert->message, message, STRING_MAX_LEN - 1);
    alert->timestamp = get_current_timestamp();
    alert->acknowledged = 0;

    pthread_mutex_unlock(&impl->mutex);

    /* 调用回调 */
    if (impl->alert_callback != NULL) {
        impl->alert_callback(alert, impl->callback_user_data);
    }

    LOG_WARNING("Alert triggered: %s - %s", name, message);
    return GNTS_OK;
}

/* 确认告警 */
int monitor_system_acknowledge_alert(monitor_system_t* monitor, const char* name) {
    if (monitor == NULL || name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->alert_count; i++) {
        if (strcmp(impl->alerts[i].name, name) == 0) {
            impl->alerts[i].acknowledged = 1;
            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Alert %s acknowledged", name);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Alert %s not found", name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 设置告警回调 */
int monitor_system_set_alert_callback(monitor_system_t* monitor,
                                       alert_callback_t callback, void* user_data) {
    if (monitor == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;
    impl->alert_callback = callback;
    impl->callback_user_data = user_data;

    return GNTS_OK;
}

/* 监控线程函数 */
static void* monitor_thread(void* arg) {
    struct monitor_system_impl* impl = (struct monitor_system_impl*)arg;

    LOG_INFO("Monitor thread started");

    while (impl->is_running) {
        /* 这里应该定期检查系统状态 */
        /* 实际实现中需要检查连接状态、延迟、资源使用等 */
        usleep(1000000); /* 1秒 */
    }

    LOG_INFO("Monitor thread stopped");
    return NULL;
}

/* 启动监控系统 */
int monitor_system_start(monitor_system_t* monitor) {
    if (monitor == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    if (impl->is_running) {
        LOG_WARNING("Monitor system is already running");
        return GNTS_OK;
    }

    impl->is_running = 1;

    /* 启动监控线程 */
    if (pthread_create(&impl->thread, NULL, monitor_thread, impl) != 0) {
        LOG_ERROR("Failed to create monitor thread");
        impl->is_running = 0;
        return GNTS_ERROR;
    }

    LOG_INFO("Monitor system started");
    return GNTS_OK;
}

/* 停止监控系统 */
int monitor_system_stop(monitor_system_t* monitor) {
    if (monitor == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct monitor_system_impl* impl = (struct monitor_system_impl*)monitor;

    if (!impl->is_running) {
        LOG_WARNING("Monitor system is not running");
        return GNTS_OK;
    }

    impl->is_running = 0;

    /* 等待线程结束 */
    pthread_join(impl->thread, NULL);

    LOG_INFO("Monitor system stopped");
    return GNTS_OK;
}