/*
 * GNTS - 观澜 · Nexus Trading System
 * 日志系统
 */

#ifndef GNTS_COMMON_LOG_H
#define GNTS_COMMON_LOG_H

#include <stdio.h>

/* 日志级别 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4,
} log_level_t;

/* 初始化日志系统 */
int log_init(const char* log_file, log_level_t level);

/* 关闭日志系统 */
void log_shutdown(void);

/* 致命错误回调 */
typedef void (*fatal_callback_t)(void* user_data);

/* 设置日志级别 */
void log_set_level(log_level_t level);

/* 设置致命错误回调（FATAL级别时在exit前调用） */
void log_set_fatal_callback(fatal_callback_t callback, void* user_data);

/* 设置是否使用彩色输出（默认开启） */
void log_set_colors(int use_colors);

/* 日志输出函数 */
void log_write(log_level_t level, const char* file, int line, const char* fmt, ...);

/* 便捷宏定义 */
#define LOG_DEBUG(fmt, ...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) log_write(LOG_LEVEL_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* GNTS_COMMON_LOG_H */