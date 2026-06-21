/*
 * GNTS - 观澜 · Nexus Trading System
 * 主头文件
 */

#ifndef GNTS_H
#define GNTS_H

/* 公共模块 */
#include "common/types.h"
#include "common/log.h"
#include "common/structures.h"

/* 数据系统 */
#include "data_system/data_manager.h"

/* 策略研究平台 */
#include "strategy/strategy_engine.h"

/* 回测引擎 */
#include "backtest/backtest_engine.h"

/* 交易执行系统 */
#include "oms/order_manager.h"

/* 风控监控系统 */
#include "risk/risk_manager.h"

/* 版本信息 */
#define GNTS_VERSION_MAJOR 1
#define GNTS_VERSION_MINOR 0
#define GNTS_VERSION_PATCH 0
#define GNTS_VERSION_STRING "1.0.0"

/* 初始化GNTS系统 */
int gnts_init(void);

/* 关闭GNTS系统 */
void gnts_shutdown(void);

/* 获取版本信息 */
const char* gnts_version(void);

#endif /* GNTS_H */