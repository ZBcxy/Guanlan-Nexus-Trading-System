/*
 * GNTS - 观澜 · Nexus Trading System
 * 日志系统实现
 */

#include "common/log.h"
#include "common/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

/* 日志级别名称 */
static const char* level_names[] = {
    "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
};

/* 日志级别颜色（ANSI） */
static const char* level_colors[] = {
    "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[35m"
};

/* 致命错误回调（可在 exit 前执行清理） */
static fatal_callback_t g_fatal_callback = NULL;
static void* g_fatal_callback_user_data = NULL;

/* 日志上下文 */
typedef struct {
    FILE* file;
    log_level_t level;
    pthread_mutex_t mutex;
    int initialized;
    int use_colors;   /* 是否输出彩色日志 */
} log_context_t;

static log_context_t g_log_ctx = {
    .file = NULL,
    .level = LOG_LEVEL_INFO,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = 0,
    .use_colors = 1
};

/* 初始化日志系统 */
int log_init(const char* log_file, log_level_t level) {
    if (g_log_ctx.initialized) {
        return GNTS_OK;
    }

    pthread_mutex_lock(&g_log_ctx.mutex);

    if (log_file != NULL) {
        g_log_ctx.file = fopen(log_file, "a");
        if (g_log_ctx.file == NULL) {
            pthread_mutex_unlock(&g_log_ctx.mutex);
            return GNTS_ERROR;
        }
    } else {
        g_log_ctx.file = stderr;
    }

    g_log_ctx.level = level;
    g_log_ctx.initialized = 1;

    pthread_mutex_unlock(&g_log_ctx.mutex);
    return GNTS_OK;
}

/* 关闭日志系统 */
void log_shutdown(void) {
    if (!g_log_ctx.initialized) {
        return;
    }

    pthread_mutex_lock(&g_log_ctx.mutex);

    if (g_log_ctx.file != NULL && g_log_ctx.file != stderr) {
        fclose(g_log_ctx.file);
    }

    g_log_ctx.file = NULL;
    g_log_ctx.initialized = 0;

    pthread_mutex_unlock(&g_log_ctx.mutex);
}

/* 设置日志级别 */
void log_set_level(log_level_t level) {
    pthread_mutex_lock(&g_log_ctx.mutex);
    g_log_ctx.level = level;
    pthread_mutex_unlock(&g_log_ctx.mutex);
}

/* 设置致命错误回调 */
void log_set_fatal_callback(fatal_callback_t callback, void* user_data) {
    pthread_mutex_lock(&g_log_ctx.mutex);
    g_fatal_callback = callback;
    g_fatal_callback_user_data = user_data;
    pthread_mutex_unlock(&g_log_ctx.mutex);
}

/* 设置是否使用彩色输出 */
void log_set_colors(int use_colors) {
    pthread_mutex_lock(&g_log_ctx.mutex);
    g_log_ctx.use_colors = use_colors;
    pthread_mutex_unlock(&g_log_ctx.mutex);
}

/* 日志输出函数 */
void log_write(log_level_t level, const char* file, int line, const char* fmt, ...) {
    if (!g_log_ctx.initialized) {
        log_init(NULL, LOG_LEVEL_INFO);
    }

    if (level < g_log_ctx.level) {
        return;
    }

    pthread_mutex_lock(&g_log_ctx.mutex);

    /* 获取当前时间（使用可重入版本） */
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* 提取文件名 */
    const char* filename = strrchr(file, '/');
    if (filename == NULL) {
        filename = file;
    } else {
        filename++;
    }

    /* 格式化日志消息 */
    char msg_buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* 输出到日志文件（带或不带颜色） */
    if (g_log_ctx.use_colors && g_log_ctx.file != stderr) {
        fprintf(g_log_ctx.file, "%s[%s] [%s] [%s:%d] %s\033[0m\n",
                level_colors[level], level_names[level],
                time_buf, filename, line, msg_buf);
    } else {
        fprintf(g_log_ctx.file, "[%s] [%s] [%s:%d] %s\n",
                time_buf, level_names[level], filename, line, msg_buf);
    }
    fflush(g_log_ctx.file);

    /* 如果是FATAL级别，调用回调后退出 */
    if (level == LOG_LEVEL_FATAL) {
        pthread_mutex_unlock(&g_log_ctx.mutex);
        if (g_fatal_callback != NULL) {
            g_fatal_callback(g_fatal_callback_user_data);
        }
        log_shutdown();
        exit(1);
    }

    pthread_mutex_unlock(&g_log_ctx.mutex);
}