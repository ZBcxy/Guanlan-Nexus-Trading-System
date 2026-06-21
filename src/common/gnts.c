/*
 * GNTS - 观澜 · Nexus Trading System
 * 主入口实现
 */

#include "gnts.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>

/* 初始化GNTS系统 */
int gnts_init(void) {
    /* 初始化日志系统 */
    int ret = log_init(NULL, LOG_LEVEL_INFO);
    if (ret != GNTS_OK) {
        fprintf(stderr, "Failed to initialize log system\n");
        return ret;
    }

    LOG_INFO("GNTS system initialized, version %s", GNTS_VERSION_STRING);
    return GNTS_OK;
}

/* 关闭GNTS系统 */
void gnts_shutdown(void) {
    LOG_INFO("GNTS system shutting down");

    /* 关闭日志系统 */
    log_shutdown();
}

/* 获取版本信息 */
const char* gnts_version(void) {
    return GNTS_VERSION_STRING;
}