/* funsos_ipc.h - 进程间通信 (IPC) API
 * 消息队列、共享内存、信号量、管道等 IPC 机制
 */

#ifndef FUNSOS_IPC_H
#define FUNSOS_IPC_H

#include "stdint.h"

/* ---- IPC 消息 ---- */
#define FUNSOS_IPC_MAX_MSG_SIZE  4096
#define FUNSOS_IPC_MAX_QUEUE_MSGS 256

typedef struct {
    uint32_t type;                          /* 消息类型 */
    uint32_t size;                          /* 消息大小 */
    uint32_t sender_pid;                    /* 发送者 PID */
    uint32_t timestamp;                     /* 发送时间戳 */
    uint8_t  data[FUNSOS_IPC_MAX_MSG_SIZE]; /* 消息数据 */
} funsos_ipc_msg_t;

/* ---- 消息队列 ---- */

typedef int funsos_mq_t;  /* 消息队列句柄 */

/* 创建消息队列 */
funsos_mq_t funsos_mq_create(const char *name, uint32_t max_msgs, uint32_t max_msg_size);

/* 打开已有消息队列 */
funsos_mq_t funsos_mq_open(const char *name);

/* 发送消息 */
int funsos_mq_send(funsos_mq_t mq, const funsos_ipc_msg_t *msg);

/* 接收消息（阻塞） */
int funsos_mq_recv(funsos_mq_t mq, funsos_ipc_msg_t *msg, uint32_t msg_type);

/* 接收消息（非阻塞） */
int funsos_mq_try_recv(funsos_mq_t mq, funsos_ipc_msg_t *msg, uint32_t msg_type);

/* 接收消息（带超时，毫秒） */
int funsos_mq_timed_recv(funsos_mq_t mq, funsos_ipc_msg_t *msg, uint32_t msg_type, uint32_t timeout_ms);

/* 获取消息队列中的消息数量 */
int funsos_mq_count(funsos_mq_t mq);

/* 清空消息队列 */
int funsos_mq_clear(funsos_mq_t mq);

/* 关闭消息队列 */
int funsos_mq_close(funsos_mq_t mq);

/* 删除消息队列 */
int funsos_mq_unlink(const char *name);

/* ---- 共享内存 ---- */

typedef int funsos_shm_t;  /* 共享内存句柄 */

/* 创建共享内存区域 */
funsos_shm_t funsos_shm_create(const char *name, uint32_t size);

/* 打开已有共享内存 */
funsos_shm_t funsos_shm_open(const char *name);

/* 映射共享内存到进程地址空间 */
void *funsos_shm_map(funsos_shm_t shm, uint32_t size);

/* 取消映射 */
int funsos_shm_unmap(void *addr, uint32_t size);

/* 获取共享内存大小 */
uint32_t funsos_shm_get_size(funsos_shm_t shm);

/* 关闭共享内存 */
int funsos_shm_close(funsos_shm_t shm);

/* 删除共享内存 */
int funsos_shm_unlink(const char *name);

/* ---- 信号量 ---- */

typedef int funsos_sem_t;  /* 信号量句柄 */

/* 创建命名信号量 */
funsos_sem_t funsos_sem_create(const char *name, uint32_t initial_value);

/* 打开已有信号量 */
funsos_sem_t funsos_sem_open(const char *name);

/* 等待信号量（P 操作，阻塞） */
int funsos_sem_wait(funsos_sem_t sem);

/* 尝试等待信号量（P 操作，非阻塞） */
int funsos_sem_try_wait(funsos_sem_t sem);

/* 等待信号量（带超时，毫秒） */
int funsos_sem_timed_wait(funsos_sem_t sem, uint32_t timeout_ms);

/* 释放信号量（V 操作） */
int funsos_sem_post(funsos_sem_t sem);

/* 获取信号量当前值 */
int funsos_sem_get_value(funsos_sem_t sem);

/* 关闭信号量 */
int funsos_sem_close(funsos_sem_t sem);

/* 删除信号量 */
int funsos_sem_unlink(const char *name);

/* ---- 管道 ---- */

/* 创建匿名管道 */
int funsos_pipe(int pipefd[2]);

/* 创建命名管道 (FIFO) */
int funsos_mkfifo(const char *path);

/* 从管道读取 */
int funsos_pipe_read(int fd, void *buf, uint32_t count);

/* 向管道写入 */
int funsos_pipe_write(int fd, const void *buf, uint32_t count);

/* ---- 事件/条件变量 ---- */

#ifndef FUNSOS_EVENT_T_DEFINED
#define FUNSOS_EVENT_T_DEFINED
typedef int funsos_ipc_event_t;  /* IPC 事件句柄 */
#endif

/* 创建事件对象 */
funsos_ipc_event_t funsos_event_create(const char *name, int manual_reset, int initial_state);

/* 打开已有事件 */
funsos_ipc_event_t funsos_event_open(const char *name);

/* 等待事件（阻塞） */
int funsos_event_wait(funsos_ipc_event_t ev);

/* 等待事件（带超时） */
int funsos_event_timed_wait(funsos_ipc_event_t ev, uint32_t timeout_ms);

/* 设置事件（有信号状态） */
int funsos_event_set(funsos_ipc_event_t ev);

/* 重置事件（无信号状态） */
int funsos_event_reset(funsos_ipc_event_t ev);

/* 触发事件（设置后立即重置） */
int funsos_event_pulse(funsos_ipc_event_t ev);

/* 关闭事件 */
int funsos_event_close(funsos_ipc_event_t ev);

/* 删除事件 */
int funsos_event_unlink(const char *name);

/* ---- 广播/多播消息 ---- */

/* 发送广播消息（所有进程可见） */
int funsos_broadcast_send(uint32_t msg_type, const void *data, uint32_t size);

/* 注册广播消息接收回调 */
int funsos_broadcast_listen(uint32_t msg_type, void (*callback)(uint32_t type, const void *data, uint32_t size, void *user_data), void *user_data);

/* 取消广播消息监听 */
int funsos_broadcast_unlisten(uint32_t msg_type);

/* ---- IPC 信息查询 ---- */

/* 列出所有消息队列 */
int funsos_ipc_list_queues(char *buf, uint32_t bufsize);

/* 列出所有共享内存区域 */
int funsos_ipc_list_shm(char *buf, uint32_t bufsize);

/* 列出所有信号量 */
int funsos_ipc_list_semaphores(char *buf, uint32_t bufsize);

/* 获取 IPC 对象信息 */
int funsos_ipc_stat(const char *name, char *buf, uint32_t bufsize);

/* 清理所有属于当前进程的 IPC 资源 */
void funsos_ipc_cleanup(void);

#endif /* FUNSOS_IPC_H */