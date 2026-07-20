/* ipc_sem.h - System V 信号量集 (Semaphore Sets)
 *
 * 补全 IPC 三件套（msg + shm + sem）的第三原语。
 *
 * 模型：每个信号量集包含 nsems 个信号量，每个信号量有 value。
 * semop() 原子地对集合中的多个信号量执行 +/- 操作。
 * 负值操作会阻塞（或返回 EAGAIN，取决于 IPC_NOWAIT）直到 value 足够。
 *
 * 与既有 ipc_msg/ipc_shm 的关系：
 *   - 三者都是 System V IPC，但相互独立，可单独使用
 *   - 都通过 evlog source="IPC" 写入诊断事件
 */
#ifndef IPC_SEM_H
#define IPC_SEM_H

#include "stdint.h"
#include "kernel_types.h"

#define IPC_SEM_MAX_SETS      32    /* 最多 32 个信号量集 */
#define IPC_SEM_MAX_NSEMS     16    /* 每个集最多 16 个信号量 */
#define IPC_SEM_MAX_UNDO      64    /* 进程退出时回滚的 semadj 记录 */
#define SEM_UNDO_FLAG         0x1000

/* semop 操作标志 */
#define SEM_NOWAIT            0x01

/* semctl 命令 */
#define SEM_GETVAL            1
#define SEM_SETVAL            2
#define SEM_GETPID            3
#define SEM_GETNCNT           4
#define SEM_GETZCNT           5
#define SEM_GETALL            6
#define SEM_SETALL            7
#define SEM_RMID              8
#define SEM_STAT              9

/* sembuf 操作标志 */
#define SEM_UNDO              0x1000

/* 单个 semop 操作 */
typedef struct sembuf {
    uint16_t sem_num;    /* 信号量在集中的索引 */
    int16_t  sem_op;     /* 操作：>0=V, <0=P, =0=等待为 0 */
    int16_t  sem_flg;    /* SEM_UNDO / SEM_NOWAIT */
} sembuf_t;

/* semid_ds 元数据 */
typedef struct semid_ds {
    int      key;            /* 用户提供的 key（或 IPC_PRIVATE=0） */
    uint32_t nsems;          /* 集中信号量个数 */
    pid_t    owner;          /* 创建者 pid */
    uint32_t ctime;         /* 最后改变 sem_op 时间 */
    uint32_t otime;         /* 最后 semop 时间 */
    uint32_t create_tick;
} semid_ds_t;

/* 信号量集统计 */
typedef struct sem_stats {
    uint32_t total_sets;
    uint32_t total_sems;
    uint32_t total_ops;        /* 累计 semop 调用 */
    uint32_t total_blocks;     /* 阻塞次数 */
    uint32_t total_wakeups;    /* 唤醒次数 */
    uint32_t total_undo_ops;
} sem_stats_t;

/* ---- 初始化 ---- */
void ipc_sem_init(void);

/* ---- 创建/获取信号量集 ----
 * key=0 表示 IPC_PRIVATE（私有）
 * 返回 semid >= 0，或 < 0 表示错误
 */
int ipc_semget(int key, int nsems, int flags);

/* ---- 信号量操作 ----
 * sop: sembuf 数组
 * nsops: 数组大小
 * 返回 0=成功，<0=错误（-1=EAGAIN 当 NOWAIT 时）
 */
int ipc_semop(int semid, const sembuf_t *sops, uint32_t nsops);

/* ---- 控制 ---- */
int ipc_semctl(int semid, int semnum, int cmd, void *arg);

/* ---- 查询 ---- */
int ipc_sem_find_by_key(int key);
uint32_t ipc_sem_list(semid_ds_t *out, uint32_t max_count);
void ipc_sem_get_stats(sem_stats_t *stats);
void ipc_sem_reset_stats(void);

/* ---- 进程退出时回滚 semadj（undo） ---- */
void ipc_sem_undo_exit(pid_t pid);

#endif /* IPC_SEM_H */
