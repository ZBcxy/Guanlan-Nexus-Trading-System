/*
 * GNTS - 观澜 · Nexus Trading System
 * 无锁队列 - 高性能线程间通信
 */

#ifndef GNTS_COMMON_LOCKFREE_QUEUE_H
#define GNTS_COMMON_LOCKFREE_QUEUE_H

#include "common/types.h"
#include <stdatomic.h>

/* 无锁队列节点 */
typedef struct queue_node {
    void* data;
    atomic_uintptr_t next;
} queue_node_t;

/* 无锁队列（MPSC - 多生产者单消费者） */
typedef struct {
    atomic_uintptr_t head;      /* 头指针 */
    atomic_uintptr_t tail;      /* 尾指针 */
    atomic_int size;            /* 队列大小 */
    int max_size;               /* 最大大小 */
    queue_node_t* node_pool;    /* 节点池 */
    atomic_int node_pool_index; /* 节点池索引 */
} lockfree_queue_t;

/* 创建无锁队列 */
lockfree_queue_t* lockfree_queue_create(int max_size);

/* 销毁无锁队列 */
void lockfree_queue_destroy(lockfree_queue_t* queue);

/* 入队（生产者） */
int lockfree_queue_push(lockfree_queue_t* queue, void* data);

/* 出队（消费者） */
void* lockfree_queue_pop(lockfree_queue_t* queue);

/* 获取队列大小 */
int lockfree_queue_size(lockfree_queue_t* queue);

/* 判断队列是否为空 */
int lockfree_queue_is_empty(lockfree_queue_t* queue);

/* 批量入队 */
int lockfree_queue_push_batch(lockfree_queue_t* queue, void** data, int count);

/* 批量出队 */
int lockfree_queue_pop_batch(lockfree_queue_t* queue, void** data, int max_count);

/* ============ 环形缓冲区（单生产者单消费者） ============ */

/* 环形缓冲区 */
typedef struct {
    void** buffer;              /* 数据缓冲区 */
    atomic_uint head;           /* 头索引 */
    atomic_uint tail;           /* 尾索引 */
    int capacity;               /* 容量 */
    int mask;                   /* 容量掩码（capacity - 1） */
} ring_buffer_t;

/* 创建环形缓冲区 */
ring_buffer_t* ring_buffer_create(int capacity);

/* 销毁环形缓冲区 */
void ring_buffer_destroy(ring_buffer_t* rb);

/* 写入数据 */
int ring_buffer_write(ring_buffer_t* rb, void* data);

/* 读取数据 */
void* ring_buffer_read(ring_buffer_t* rb);

/* 获取可读数量 */
int ring_buffer_readable(ring_buffer_t* rb);

/* 获取可写数量 */
int ring_buffer_writable(ring_buffer_t* rb);

/* ============ 高性能消息队列 ============ */

/* 消息类型 */
typedef enum {
    MSG_TYPE_TICK = 1,
    MSG_TYPE_BAR = 2,
    MSG_TYPE_ORDER = 3,
    MSG_TYPE_TRADE = 4,
    MSG_TYPE_SIGNAL = 5,
} message_type_t;

/* 消息结构 */
typedef struct {
    message_type_t type;
    timestamp_t timestamp;
    void* data;
    int data_size;
} message_t;

/* 消息队列 */
typedef struct {
    lockfree_queue_t* queue;
    atomic_int message_count;
    int max_messages;
} message_queue_t;

/* 创建消息队列 */
message_queue_t* message_queue_create(int max_size);

/* 销毁消息队列 */
void message_queue_destroy(message_queue_t* mq);

/* 发送消息 */
int message_queue_send(message_queue_t* mq, message_type_t type, void* data, int size);

/* 接收消息 */
message_t* message_queue_recv(message_queue_t* mq);

/* 获取消息数量 */
int message_queue_count(message_queue_t* mq);

#endif /* GNTS_COMMON_LOCKFREE_QUEUE_H */