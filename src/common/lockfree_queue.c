/*
 * GNTS - 观澜 · Nexus Trading System
 * 无锁队列实现
 */

#include "common/lockfree_queue.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <sched.h>

/* 创建无锁队列 */
lockfree_queue_t* lockfree_queue_create(int max_size) {
    lockfree_queue_t* queue = (lockfree_queue_t*)malloc(sizeof(lockfree_queue_t));
    if (queue == NULL) {
        LOG_ERROR("Failed to allocate lockfree queue");
        return NULL;
    }

    /* 创建节点池 */
    queue->node_pool = (queue_node_t*)malloc(sizeof(queue_node_t) * max_size);
    if (queue->node_pool == NULL) {
        free(queue);
        LOG_ERROR("Failed to allocate node pool");
        return NULL;
    }

    /* 初始化节点池 */
    for (int i = 0; i < max_size; i++) {
        queue->node_pool[i].data = NULL;
        atomic_init(&queue->node_pool[i].next, 0);
    }

    atomic_init(&queue->node_pool_index, 0);
    atomic_init(&queue->head, 0);
    atomic_init(&queue->tail, 0);
    atomic_init(&queue->size, 0);
    queue->max_size = max_size;

    LOG_INFO("Lockfree queue created with max_size=%d", max_size);
    return queue;
}

/* 销毁无锁队列 */
void lockfree_queue_destroy(lockfree_queue_t* queue) {
    if (queue == NULL) {
        return;
    }

    if (queue->node_pool != NULL) {
        free(queue->node_pool);
    }

    free(queue);
    LOG_INFO("Lockfree queue destroyed");
}

/* 从节点池获取节点 */
static queue_node_t* get_node_from_pool(lockfree_queue_t* queue) {
    int index = atomic_fetch_add(&queue->node_pool_index, 1);
    if (index >= queue->max_size) {
        atomic_fetch_sub(&queue->node_pool_index, 1);
        return NULL;
    }

    return &queue->node_pool[index];
}

/* 入队（多生产者） */
int lockfree_queue_push(lockfree_queue_t* queue, void* data) {
    if (queue == NULL || data == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    /* 检查队列是否已满 */
    if (atomic_load_explicit(&queue->size, memory_order_relaxed) >= queue->max_size) {
        LOG_WARNING("Queue is full");
        return GNTS_ERROR;
    }

    /* 从节点池获取节点 */
    queue_node_t* node = get_node_from_pool(queue);
    if (node == NULL) {
        LOG_ERROR("Failed to get node from pool");
        return GNTS_ERROR_MEMORY;
    }

    node->data = data;
    atomic_store_explicit(&node->next, (uintptr_t)0, memory_order_relaxed);

    /* 添加到队列尾部 */
    uintptr_t old_tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    uintptr_t new_tail = (uintptr_t)node;
    int backoff = 0;

    /* CAS操作（带指数退避） */
    while (!atomic_compare_exchange_weak_explicit(&queue->tail, &old_tail, new_tail,
            memory_order_release, memory_order_relaxed)) {
        /* 指数退避：减少高竞争下的 CPU 空转 */
        if (backoff < 1024) {
            for (int spin = 0; spin < (1 << (backoff > 10 ? 10 : backoff)); spin++) {
                /* 编译器屏障，防止优化掉空循环 */
                __sync_synchronize();
            }
            backoff++;
        } else {
            /* 退避到让出 CPU */
            sched_yield();
        }
    }

    /* 更新前一个节点的next指针（release 确保数据对消费者可见） */
    if (old_tail != 0) {
        queue_node_t* prev_node = (queue_node_t*)old_tail;
        atomic_store_explicit(&prev_node->next, new_tail, memory_order_release);
    } else {
        /* 队列之前为空，更新head */
        atomic_store_explicit(&queue->head, new_tail, memory_order_release);
    }

    atomic_fetch_add_explicit(&queue->size, 1, memory_order_relaxed);

    LOG_DEBUG("Pushed data to queue, size=%d", atomic_load_explicit(&queue->size, memory_order_relaxed));
    return GNTS_OK;
}

/* 出队（单消费者） */
void* lockfree_queue_pop(lockfree_queue_t* queue) {
    if (queue == NULL) {
        return NULL;
    }

    /* 检查队列是否为空 */
    uintptr_t head = atomic_load_explicit(&queue->head, memory_order_acquire);
    if (head == 0) {
        return NULL;
    }

    queue_node_t* node = (queue_node_t*)head;

    /* acquire 屏障：确保读取节点内容前 head 已同步 */
    uintptr_t next = atomic_load_explicit(&node->next, memory_order_acquire);

    /* CAS操作 */
    if (atomic_compare_exchange_strong_explicit(&queue->head, &head, next,
            memory_order_release, memory_order_relaxed)) {
        void* data = node->data;
        node->data = NULL;

        atomic_fetch_sub_explicit(&queue->size, 1, memory_order_relaxed);

        /* 如果队列为空，重置tail */
        if (next == 0) {
            atomic_store_explicit(&queue->tail, (uintptr_t)0, memory_order_release);
        }

        LOG_DEBUG("Popped data from queue, size=%d", atomic_load_explicit(&queue->size, memory_order_relaxed));
        return data;
    }

    return NULL;
}

/* 获取队列大小 */
int lockfree_queue_size(lockfree_queue_t* queue) {
    if (queue == NULL) {
        return 0;
    }

    return atomic_load_explicit(&queue->size, memory_order_acquire);
}

/* 判断队列是否为空 */
int lockfree_queue_is_empty(lockfree_queue_t* queue) {
    if (queue == NULL) {
        return 1;
    }

    return atomic_load_explicit(&queue->size, memory_order_acquire) == 0;
}

/* 批量入队 */
int lockfree_queue_push_batch(lockfree_queue_t* queue, void** data, int count) {
    if (queue == NULL || data == NULL || count <= 0) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    int success_count = 0;
    for (int i = 0; i < count; i++) {
        if (lockfree_queue_push(queue, data[i]) == GNTS_OK) {
            success_count++;
        }
    }

    LOG_DEBUG("Pushed %d/%d items to queue", success_count, count);
    return success_count;
}

/* 批量出队 */
int lockfree_queue_pop_batch(lockfree_queue_t* queue, void** data, int max_count) {
    if (queue == NULL || data == NULL || max_count <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < max_count; i++) {
        void* item = lockfree_queue_pop(queue);
        if (item != NULL) {
            data[i] = item;
            count++;
        } else {
            break;
        }
    }

    LOG_DEBUG("Popped %d items from queue", count);
    return count;
}

/* ============ 环形缓冲区实现 ============ */

/* 创建环形缓冲区 */
ring_buffer_t* ring_buffer_create(int capacity) {
    /* 容量必须是2的幂 */
    int actual_capacity = 1;
    while (actual_capacity < capacity) {
        actual_capacity *= 2;
    }

    ring_buffer_t* rb = (ring_buffer_t*)malloc(sizeof(ring_buffer_t));
    if (rb == NULL) {
        LOG_ERROR("Failed to allocate ring buffer");
        return NULL;
    }

    rb->buffer = (void**)malloc(sizeof(void*) * actual_capacity);
    if (rb->buffer == NULL) {
        free(rb);
        LOG_ERROR("Failed to allocate buffer");
        return NULL;
    }

    rb->capacity = actual_capacity;
    rb->mask = actual_capacity - 1;
    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);

    LOG_INFO("Ring buffer created with capacity=%d", actual_capacity);
    return rb;
}

/* 销毁环形缓冲区 */
void ring_buffer_destroy(ring_buffer_t* rb) {
    if (rb == NULL) {
        return;
    }

    if (rb->buffer != NULL) {
        free(rb->buffer);
    }

    free(rb);
    LOG_INFO("Ring buffer destroyed");
}

/* 写入数据（SPSC — 生产者） */
int ring_buffer_write(ring_buffer_t* rb, void* data) {
    if (rb == NULL || data == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    unsigned int head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    unsigned int tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    /* 检查是否已满 */
    if (((head + 1) & rb->mask) == tail) {
        LOG_WARNING("Ring buffer is full");
        return GNTS_ERROR;
    }

    /* 写入数据（必须在 head 更新之前对读者可见） */
    rb->buffer[head] = data;

    /* release 屏障：确保 buffer 写入在 head 更新前对所有 CPU 可见 */
    atomic_store_explicit(&rb->head, (head + 1) & rb->mask, memory_order_release);

    return GNTS_OK;
}

/* 读取数据（SPSC — 消费者） */
void* ring_buffer_read(ring_buffer_t* rb) {
    if (rb == NULL) {
        return NULL;
    }

    unsigned int tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

    /* acquire 屏障：确保读到最新 head 后，对应的 buffer 数据可见 */
    unsigned int head = atomic_load_explicit(&rb->head, memory_order_acquire);

    /* 检查是否为空 */
    if (tail == head) {
        return NULL;
    }

    void* data = rb->buffer[tail];

    /* release 屏障：确保数据读取在 tail 更新前完成 */
    atomic_store_explicit(&rb->tail, (tail + 1) & rb->mask, memory_order_release);

    return data;
}

/* 获取可读数量 */
int ring_buffer_readable(ring_buffer_t* rb) {
    if (rb == NULL) {
        return 0;
    }

    unsigned int head = atomic_load_explicit(&rb->head, memory_order_acquire);
    unsigned int tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

    return (head - tail) & rb->mask;
}

/* 获取可写数量 */
int ring_buffer_writable(ring_buffer_t* rb) {
    if (rb == NULL) {
        return 0;
    }

    return rb->capacity - ring_buffer_readable(rb) - 1;  /* 保留1个槽位区分空/满 */
}

/* ============ 消息队列实现 ============ */

/* 创建消息队列 */
message_queue_t* message_queue_create(int max_size) {
    message_queue_t* mq = (message_queue_t*)malloc(sizeof(message_queue_t));
    if (mq == NULL) {
        LOG_ERROR("Failed to allocate message queue");
        return NULL;
    }

    mq->queue = lockfree_queue_create(max_size);
    if (mq->queue == NULL) {
        free(mq);
        return NULL;
    }

    atomic_init(&mq->message_count, 0);
    mq->max_messages = max_size;

    LOG_INFO("Message queue created with max_size=%d", max_size);
    return mq;
}

/* 销毁消息队列 */
void message_queue_destroy(message_queue_t* mq) {
    if (mq == NULL) {
        return;
    }

    if (mq->queue != NULL) {
        lockfree_queue_destroy(mq->queue);
    }

    free(mq);
    LOG_INFO("Message queue destroyed");
}

/* 发送消息 */
int message_queue_send(message_queue_t* mq, message_type_t type, void* data, int size) {
    if (mq == NULL || data == NULL) {
        return GNTS_ERROR_INVALID_PARAM;
    }

    message_t* msg = (message_t*)malloc(sizeof(message_t));
    if (msg == NULL) {
        LOG_ERROR("Failed to allocate message");
        return GNTS_ERROR_MEMORY;
    }

    msg->type = type;
    msg->timestamp = get_current_timestamp();
    msg->data = data;
    msg->data_size = size;

    int ret = lockfree_queue_push(mq->queue, msg);
    if (ret == GNTS_OK) {
        atomic_fetch_add_explicit(&mq->message_count, 1, memory_order_relaxed);
    } else {
        free(msg);
    }

    return ret;
}

/* 接收消息 */
message_t* message_queue_recv(message_queue_t* mq) {
    if (mq == NULL) {
        return NULL;
    }

    message_t* msg = (message_t*)lockfree_queue_pop(mq->queue);
    if (msg != NULL) {
        atomic_fetch_sub_explicit(&mq->message_count, 1, memory_order_relaxed);
    }

    return msg;
}

/* 获取消息数量 */
int message_queue_count(message_queue_t* mq) {
    if (mq == NULL) {
        return 0;
    }

    return atomic_load_explicit(&mq->message_count, memory_order_acquire);
}