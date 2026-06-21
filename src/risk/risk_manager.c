/*
 * GNTS - 观澜 · Nexus Trading System
 * 风控管理器实现
 */

#include "risk/risk_manager.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

/* 风控管理器内部结构 */
struct risk_manager_impl {
    risk_rule_t* rules;
    int rule_count;
    int max_rules;
    int is_enabled;
    pthread_mutex_t mutex;
};

/* 创建风控管理器 */
risk_manager_t* risk_manager_create(void) {
    struct risk_manager_impl* manager = (struct risk_manager_impl*)malloc(sizeof(struct risk_manager_impl));
    if (manager == NULL) {
        LOG_ERROR("Failed to allocate memory for risk manager");
        return NULL;
    }

    manager->rules = (risk_rule_t*)malloc(sizeof(risk_rule_t) * 50);
    manager->rule_count = 0;
    manager->max_rules = 50;
    manager->is_enabled = 1;
    pthread_mutex_init(&manager->mutex, NULL);

    LOG_INFO("Risk manager created");
    return (risk_manager_t*)manager;
}

/* 销毁风控管理器 */
void risk_manager_destroy(risk_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    pthread_mutex_destroy(&impl->mutex);

    /* 注意：规则由调用者通过 risk_rule_destroy() 释放
     * 这里只释放规则数组本身（浅拷贝存储） */
    if (impl->rules != NULL) {
        free(impl->rules);
    }

    free(impl);
    LOG_INFO("Risk manager destroyed");
}

/* 添加风控规则 */
int risk_manager_add_rule(risk_manager_t* manager, const risk_rule_t* rule) {
    if (manager == NULL || rule == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    if (impl->rule_count >= impl->max_rules) {
        /* 扩容 */
        int new_max = impl->max_rules * 2;
        risk_rule_t* new_rules = (risk_rule_t*)realloc(
            impl->rules, sizeof(risk_rule_t) * new_max);
        if (new_rules == NULL) {
            pthread_mutex_unlock(&impl->mutex);
            LOG_ERROR("Failed to expand rules array");
            return GNTS_ERROR_MEMORY;
        }
        impl->rules = new_rules;
        impl->max_rules = new_max;
    }

    impl->rules[impl->rule_count++] = *rule;

    pthread_mutex_unlock(&impl->mutex);
    LOG_INFO("Risk rule %s added", rule->name);
    return GNTS_OK;
}

/* 移除风控规则 */
int risk_manager_remove_rule(risk_manager_t* manager, const char* rule_name) {
    if (manager == NULL || rule_name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    int found = -1;
    for (int i = 0; i < impl->rule_count; i++) {
        if (strcmp(impl->rules[i].name, rule_name) == 0) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        /* 释放参数 */
        if (impl->rules[found].params != NULL) {
            free(impl->rules[found].params);
        }

        /* 移除规则 */
        for (int i = found; i < impl->rule_count - 1; i++) {
            impl->rules[i] = impl->rules[i + 1];
        }
        impl->rule_count--;

        LOG_INFO("Risk rule %s removed", rule_name);
    }

    pthread_mutex_unlock(&impl->mutex);
    return found >= 0 ? GNTS_OK : GNTS_ERROR_NOT_FOUND;
}

/* 启用风控规则 */
int risk_manager_enable_rule(risk_manager_t* manager, const char* rule_name) {
    if (manager == NULL || rule_name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->rule_count; i++) {
        if (strcmp(impl->rules[i].name, rule_name) == 0) {
            impl->rules[i].enabled = 1;
            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Risk rule %s enabled", rule_name);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Risk rule %s not found", rule_name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 禁用风控规则 */
int risk_manager_disable_rule(risk_manager_t* manager, const char* rule_name) {
    if (manager == NULL || rule_name == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    pthread_mutex_lock(&impl->mutex);

    for (int i = 0; i < impl->rule_count; i++) {
        if (strcmp(impl->rules[i].name, rule_name) == 0) {
            impl->rules[i].enabled = 0;
            pthread_mutex_unlock(&impl->mutex);
            LOG_INFO("Risk rule %s disabled", rule_name);
            return GNTS_OK;
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_ERROR("Risk rule %s not found", rule_name);
    return GNTS_ERROR_NOT_FOUND;
}

/* 检查订单（事前风控） */
int risk_manager_check_order(risk_manager_t* manager, const risk_context_t* context,
                             risk_check_result_t* result) {
    if (manager == NULL || context == NULL || result == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;

    if (!impl->is_enabled) {
        result->passed = 1;
        result->failed_rule = NULL;
        result->message[0] = '\0';
        return GNTS_OK;
    }

    pthread_mutex_lock(&impl->mutex);

    result->passed = 1;
    result->failed_rule = NULL;
    result->message[0] = '\0';

    /* 检查所有规则 */
    for (int i = 0; i < impl->rule_count; i++) {
        if (impl->rules[i].enabled && impl->rules[i].check != NULL) {
            int check_result = impl->rules[i].check(&impl->rules[i], context);

            if (check_result != 0) {
                result->passed = 0;
                result->failed_rule = &impl->rules[i];
                snprintf(result->message, STRING_MAX_LEN,
                         "Risk rule %s check failed", impl->rules[i].name);

                pthread_mutex_unlock(&impl->mutex);
                LOG_WARNING("Order rejected by risk rule %s", impl->rules[i].name);
                return GNTS_OK;
            }
        }
    }

    pthread_mutex_unlock(&impl->mutex);
    LOG_DEBUG("Order passed all risk checks");
    return GNTS_OK;
}

/* 启用风控 */
int risk_manager_enable(risk_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;
    impl->is_enabled = 1;

    LOG_INFO("Risk manager enabled");
    return GNTS_OK;
}

/* 禁用风控 */
int risk_manager_disable(risk_manager_t* manager) {
    if (manager == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    struct risk_manager_impl* impl = (struct risk_manager_impl*)manager;
    impl->is_enabled = 0;

    LOG_WARNING("Risk manager disabled");
    return GNTS_OK;
}

/* ============ 预定义风控规则实现 ============ */

/* 持仓限制规则检查函数 */
static int check_position_limit(risk_rule_t* rule, const void* ctx) {
    const risk_context_t* context = (const risk_context_t*)ctx;
    double max_position_ratio = *(double*)rule->params;

    if (context->position != NULL) {
        double position_ratio = context->position->market_value / context->account->total_asset;
        if (position_ratio > max_position_ratio) {
            LOG_WARNING("Position ratio %.2f exceeds limit %.2f",
                       position_ratio, max_position_ratio);
            return -1;
        }
    }

    return 0;
}

/* 创建持仓限制规则 */
risk_rule_t* risk_rule_create_position_limit(const char* name, double max_position_ratio) {
    risk_rule_t* rule = (risk_rule_t*)malloc(sizeof(risk_rule_t));
    if (rule == NULL) {
        return NULL;
    }

    strncpy(rule->name, name, STRING_MAX_LEN - 1);
    rule->type = RISK_RULE_POSITION_LIMIT;
    rule->action = RISK_ACTION_REJECT;
    rule->enabled = 1;
    rule->check = check_position_limit;

    rule->params = malloc(sizeof(double));
    if (rule->params == NULL) {
        free(rule);
        return NULL;
    }
    *(double*)rule->params = max_position_ratio;

    return rule;
}

/* 订单限制规则检查函数 */
static int check_order_limit(risk_rule_t* rule, const void* ctx) {
    const risk_context_t* context = (const risk_context_t*)ctx;
    /* params 布局: double[0]覆盖2个int, double[1]存max_order_value */
    int max_orders_per_day = ((int*)rule->params)[0];
    int* current_order_count = &((int*)rule->params)[1];
    double max_order_value = ((double*)rule->params)[1];

    if (context->order != NULL) {
        /* 检查当日订单数量（使用指针避免越界） */
        if (*current_order_count >= max_orders_per_day) {
            LOG_WARNING("Daily order count %d exceeds limit %d",
                       *current_order_count, max_orders_per_day);
            return -1;
        }

        /* 检查订单金额 */
        double order_value = context->order->price * context->order->quantity;
        if (order_value > max_order_value) {
            LOG_WARNING("Order value %.2f exceeds limit %.2f",
                       order_value, max_order_value);
            return -1;
        }

        /* 通过检查，增加计数（使用正确的偏移） */
        (*current_order_count)++;
    }

    return 0;
}

/* 创建订单限制规则 */
risk_rule_t* risk_rule_create_order_limit(const char* name, int max_orders_per_day,
                                          double max_order_value) {
    risk_rule_t* rule = (risk_rule_t*)malloc(sizeof(risk_rule_t));
    if (rule == NULL) {
        return NULL;
    }

    strncpy(rule->name, name, STRING_MAX_LEN - 1);
    rule->type = RISK_RULE_ORDER_LIMIT;
    rule->action = RISK_ACTION_REJECT;
    rule->enabled = 1;
    rule->check = check_order_limit;

    /* params 布局: [int max_orders_per_day] [int current_count] [double max_order_value]
     * 使用 double 数组确保对齐，[0] 低32位存 max_orders，[0] 高32位存 current_count，[1] 存 max_order_value */
    size_t params_size = sizeof(double) * 2;  /* 足够容纳2个int + 1个double */
    rule->params = calloc(1, params_size);
    if (rule->params == NULL) {
        free(rule);
        return NULL;
    }
    ((int*)rule->params)[0] = max_orders_per_day;
    /* ((int*)rule->params)[1] = 0; — current_count，calloc 已置零 */
    ((double*)rule->params)[1] = max_order_value;

    return rule;
}

/* 亏损限制规则检查函数 */
static int check_loss_limit(risk_rule_t* rule, const void* ctx) {
    const risk_context_t* context = (const risk_context_t*)ctx;
    double max_loss_ratio = ((double*)rule->params)[0];
    double max_loss_amount = ((double*)rule->params)[1];

    if (context->account != NULL) {
        double loss_ratio = -context->account->profit_loss / context->account->total_asset;
        if (loss_ratio > max_loss_ratio || -context->account->profit_loss > max_loss_amount) {
            LOG_WARNING("Loss ratio %.2f or amount %.2f exceeds limit",
                       loss_ratio, -context->account->profit_loss);
            return -1;
        }
    }

    return 0;
}

/* 创建亏损限制规则 */
risk_rule_t* risk_rule_create_loss_limit(const char* name, double max_loss_ratio,
                                         double max_loss_amount) {
    risk_rule_t* rule = (risk_rule_t*)malloc(sizeof(risk_rule_t));
    if (rule == NULL) {
        return NULL;
    }

    strncpy(rule->name, name, STRING_MAX_LEN - 1);
    rule->type = RISK_RULE_LOSS_LIMIT;
    rule->action = RISK_ACTION_BLOCK;
    rule->enabled = 1;
    rule->check = check_loss_limit;

    rule->params = malloc(sizeof(double) * 2);
    if (rule->params == NULL) {
        free(rule);
        return NULL;
    }
    ((double*)rule->params)[0] = max_loss_ratio;
    ((double*)rule->params)[1] = max_loss_amount;

    return rule;
}

/* 黑名单参数结构 */
typedef struct {
    char** symbols;
    int count;
    int max_count;
} blacklist_params_t;

/* 黑名单规则检查函数 */
static int check_blacklist(risk_rule_t* rule, const void* ctx) {
    const risk_context_t* context = (const risk_context_t*)ctx;
    blacklist_params_t* bl = (blacklist_params_t*)rule->params;

    if (context->order != NULL && bl != NULL) {
        for (int i = 0; i < bl->count; i++) {
            if (strcmp(context->order->symbol, bl->symbols[i]) == 0) {
                LOG_WARNING("Symbol %s is in blacklist", context->order->symbol);
                return -1;
            }
        }
    }

    return 0;
}

/* 创建黑名单规则 */
risk_rule_t* risk_rule_create_blacklist(const char* name, const char** symbols, int count) {
    risk_rule_t* rule = (risk_rule_t*)malloc(sizeof(risk_rule_t));
    if (rule == NULL) {
        return NULL;
    }

    strncpy(rule->name, name, STRING_MAX_LEN - 1);
    rule->type = RISK_RULE_BLACKLIST;
    rule->action = RISK_ACTION_REJECT;
    rule->enabled = 1;
    rule->check = check_blacklist;

    /* 分配黑名单参数结构 */
    blacklist_params_t* bl = (blacklist_params_t*)malloc(sizeof(blacklist_params_t));
    if (bl == NULL) {
        free(rule);
        return NULL;
    }

    int max_count = (count < 100) ? 100 : count;  /* 至少100个槽位 */
    bl->symbols = (char**)calloc(max_count, sizeof(char*));
    if (bl->symbols == NULL) {
        free(bl);
        free(rule);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        bl->symbols[i] = strdup(symbols[i]);
    }
    bl->count = count;
    bl->max_count = max_count;
    rule->params = bl;

    return rule;
}

/* 价格偏离规则检查函数 */
static int check_price_deviation(risk_rule_t* rule, const void* ctx) {
    const risk_context_t* context = (const risk_context_t*)ctx;
    double max_deviation_ratio = *(double*)rule->params;

    if (context->order != NULL && context->bar != NULL) {
        double deviation = fabs(context->order->price - context->bar->close) / context->bar->close;
        if (deviation > max_deviation_ratio) {
            LOG_WARNING("Price deviation %.4f exceeds limit %.4f",
                       deviation, max_deviation_ratio);
            return -1;
        }
    }

    return 0;
}

/* 创建价格偏离规则 */
risk_rule_t* risk_rule_create_price_deviation(const char* name, double max_deviation_ratio) {
    risk_rule_t* rule = (risk_rule_t*)malloc(sizeof(risk_rule_t));
    if (rule == NULL) {
        return NULL;
    }

    strncpy(rule->name, name, STRING_MAX_LEN - 1);
    rule->type = RISK_RULE_PRICE_DEVIATION;
    rule->action = RISK_ACTION_REJECT;
    rule->enabled = 1;
    rule->check = check_price_deviation;

    rule->params = malloc(sizeof(double));
    if (rule->params == NULL) {
        free(rule);
        return NULL;
    }
    *(double*)rule->params = max_deviation_ratio;

    return rule;
}

/* 销毁风控规则 */
void risk_rule_destroy(risk_rule_t* rule) {
    if (rule == NULL) {
        return;
    }

    if (rule->params != NULL) {
        /* 根据规则类型释放参数 */
        if (rule->type == RISK_RULE_BLACKLIST) {
            blacklist_params_t* bl = (blacklist_params_t*)rule->params;
            for (int i = 0; i < bl->count; i++) {
                if (bl->symbols[i] != NULL) {
                    free(bl->symbols[i]);
                }
            }
            free(bl->symbols);
        }
        free(rule->params);
    }

    free(rule);
}