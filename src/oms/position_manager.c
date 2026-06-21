/*
 * GNTS - 观澜 · Nexus Trading System
 * 仓位管理器实现
 */

#include "oms/order_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* 仓位管理器内部结构 */
struct position_manager_impl {
    position_t* positions;
    int position_count;
    int max_positions;
    account_info_t account;
    pthread_mutex_t mutex;
};

/* 创建仓位管理器 */
position_manager_t* position_manager_create(void) {
    struct position_manager_impl* manager = (struct position_manager_impl*)malloc(sizeof(struct position_manager_impl));
    if (manager == NULL) {
        LOG_ERROR("Failed to allocate memory for position manager");
        return NULL;
    }

    manager->positions = NULL;
    manager->position_count = 0;
    manager->max_positions = 0;
    manager->account.total_asset = 0.0;
    manager->account.cash = 0.0;
    manager->account.market_value = 0.0;
    manager->account.profit_loss = 0.0;
    manager->account.available_cash = 0.0;
    manager->account.margin = 0.0;
    pthread_mutex_init(&manager->mutex, NULL);

    LOG_INFO("Position manager created");
    return (position_manager_t*)manager;
}

/* 销毁仓位管理器 */
void position_manager_destroy(position_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_destroy(&impl->mutex);

    if (impl->positions != NULL) {
        free(impl->positions);
    }

    free(impl);
    LOG_INFO("Position manager destroyed");
}

/* 更新持仓 */
int position_manager_update(position_manager_t* manager, const position_t* position) {
    if (manager == NULL || position == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    /* 查找是否已存在 */
    int found = -1;
    for (int i = 0; i < impl->position_count; i++) {
        if (strcmp(impl->positions[i].symbol, position->symbol) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        /* 更新持仓 */
        impl->positions[found] = *position;
    } else {
        /* 新增持仓（动态扩容） */
        DYNAMIC_ARRAY_ENSURE_CAPACITY(impl->positions, impl->position_count,
                                      impl->max_positions, sizeof(position_t), 64);

        impl->positions[impl->position_count++] = *position;
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_INFO("Position updated: %s", position->symbol);
    return GNTS_OK;
}

/* 查询持仓 */
int position_manager_query(position_manager_t* manager, const char* symbol, position_t* position) {
    if (manager == NULL || symbol == NULL || position == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->position_count; i++) {
        if (strcmp(impl->positions[i].symbol, symbol) == 0) {
            *position = impl->positions[i];
            pthread_mutex_unlock(&impl->mutex);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Position %s not found", symbol);
    return GNTS_ERROR_NOT_FOUND;
}

/* 查询所有持仓 */
int position_manager_query_all(position_manager_t* manager, position_t** positions, int* count) {
    if (manager == NULL || positions == NULL || count == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);
    *positions = impl->positions;
    *count = impl->position_count;
    pthread_mutex_unlock(&impl->mutex);

    return GNTS_OK;
}

/* 更新账户信息 */
int position_manager_update_account(position_manager_t* manager, const account_info_t* account) {
    if (manager == NULL || account == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);
    impl->account = *account;
    pthread_mutex_unlock(&impl->mutex);

    LOG_INFO("Account updated");
    return GNTS_OK;
}

/* 查询账户信息 */
int position_manager_query_account(position_manager_t* manager, account_info_t* account) {
    if (manager == NULL || account == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);
    *account = impl->account;
    pthread_mutex_unlock(&impl->mutex);

    return GNTS_OK;
}

/* 计算市值 */
double position_manager_calculate_market_value(position_manager_t* manager) {
    if (manager == NULL) {
        return 0.0;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    double total_value = 0.0;
    for (int i = 0; i < impl->position_count; i++) {
        total_value += impl->positions[i].market_value;
    }

    pthread_mutex_unlock(&impl->mutex);

    LOG_DEBUG("Total market value: %.2f", total_value);
    return total_value;
}

/* 计算盈亏 */
double position_manager_calculate_profit_loss(position_manager_t* manager) {
    if (manager == NULL) {
        return 0.0;
    }

    struct position_manager_impl* impl = (struct position_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    double total_pl = 0.0;
    for (int i = 0; i < impl->position_count; i++) {
        total_pl += impl->positions[i].profit_loss;
    }

    pthread_mutex_unlock(&impl->mutex);

    LOG_DEBUG("Total profit/loss: %.2f", total_pl);
    return total_pl;
}