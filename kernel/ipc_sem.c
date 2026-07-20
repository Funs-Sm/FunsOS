/* ipc_sem.c - System V 信号量集实现
 *
 * 简化设计：
 *   - 静态数组 IPC_SEM_MAX_SETS 个信号量集
 *   - 阻塞通过忙等待+轮询实现（单核 hobbyist OS，无抢占）
 *   - undo 记录简单数组
 *   - 所有公开函数使用 ipc_ 前缀，避免与 sync.h 的 sem_t/sem_init 冲突
 */
#include "ipc_sem.h"
#include "evlog.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdlib.h"
#include "timer.h"
#include "sched.h"
#include "kernel_types.h"
#include "kernel_proc.h"

#define IPC_EV_SOURCE "IPC"

/* 单个信号量（重命名为 ipc_sem_unit_t 避免与 sync.h 的 sem_t 冲突） */
typedef struct ipc_sem_unit {
    int16_t  value;       /* 当前值 */
    uint16_t wait_cnt;   /* 等待 value 增加的进程数 */
    uint16_t wait_zero;  /* 等待 value 变为 0 的进程数 */
    pid_t    last_pid;   /* 最后操作此信号量的进程 */
} ipc_sem_unit_t;

/* 信号量集 */
typedef struct sem_set {
    int             in_use;
    int             key;
    uint32_t        nsems;
    pid_t           owner;
    uint32_t        ctime;
    uint32_t        otime;
    uint32_t        create_tick;
    ipc_sem_unit_t  sems[IPC_SEM_MAX_NSEMS];
} sem_set_t;

/* undo 记录：进程退出时回滚其 sem_op */
typedef struct sem_undo {
    pid_t    pid;
    int      semid;
    uint16_t sem_num;
    int16_t  adjval;   /* 累计调整值（取反） */
} sem_undo_t;

/* 全局状态 */
static struct {
    spinlock_t  lock;
    sem_set_t   sets[IPC_SEM_MAX_SETS];
    sem_undo_t  undo[IPC_SEM_MAX_UNDO];
    uint32_t    undo_count;
    sem_stats_t stats;
} g_sem;

/* ---- 内部辅助 ---- */

static sem_set_t *ipc_sem_find_internal(int semid) {
    if (semid < 0 || semid >= IPC_SEM_MAX_SETS) return NULL;
    sem_set_t *s = &g_sem.sets[semid];
    if (!s->in_use) return NULL;
    return s;
}

static sem_set_t *ipc_sem_find_by_key_internal(int key) {
    if (key == 0) return NULL; /* IPC_PRIVATE */
    for (uint32_t i = 0; i < IPC_SEM_MAX_SETS; i++) {
        if (g_sem.sets[i].in_use && g_sem.sets[i].key == key) {
            return &g_sem.sets[i];
        }
    }
    return NULL;
}

/* 记录 undo */
static void ipc_sem_undo_record(pid_t pid, int semid, uint16_t sem_num, int16_t op) {
    if (op == 0) return;
    for (uint32_t i = 0; i < g_sem.undo_count; i++) {
        if (g_sem.undo[i].pid == pid &&
            g_sem.undo[i].semid == semid &&
            g_sem.undo[i].sem_num == sem_num) {
            g_sem.undo[i].adjval -= op;
            return;
        }
    }
    if (g_sem.undo_count >= IPC_SEM_MAX_UNDO) return;
    g_sem.undo[g_sem.undo_count].pid = pid;
    g_sem.undo[g_sem.undo_count].semid = semid;
    g_sem.undo[g_sem.undo_count].sem_num = sem_num;
    g_sem.undo[g_sem.undo_count].adjval = -op;
    g_sem.undo_count++;
}

/* ---- 公共 API ---- */

void ipc_sem_init(void) {
    memset(&g_sem, 0, sizeof(g_sem));
    spinlock_init(&g_sem.lock);
    evlog_register_source(IPC_EV_SOURCE);
    klog_info("ipc_sem: semaphore subsystem initialized (%u max sets)",
              IPC_SEM_MAX_SETS);
}

int ipc_semget(int key, int nsems, int flags) {
    (void)flags;
    if (nsems <= 0 || nsems > IPC_SEM_MAX_NSEMS) return -1;

    spinlock_lock(&g_sem.lock);

    sem_set_t *s = NULL;
    if (key != 0) {
        s = ipc_sem_find_by_key_internal(key);
        if (s) {
            if ((uint32_t)nsems > s->nsems) {
                spinlock_unlock(&g_sem.lock);
                return -2;
            }
            spinlock_unlock(&g_sem.lock);
            return (int)(s - g_sem.sets);
        }
    }

    for (uint32_t i = 0; i < IPC_SEM_MAX_SETS; i++) {
        if (!g_sem.sets[i].in_use) {
            s = &g_sem.sets[i];
            memset(s, 0, sizeof(*s));
            s->in_use = 1;
            s->key = key;
            s->nsems = (uint32_t)nsems;
            s->owner = sched_get_current() ? sched_get_current()->pid : 0;
            s->create_tick = (uint32_t)timer_get_ticks();
            s->ctime = s->create_tick;
            g_sem.stats.total_sets++;
            g_sem.stats.total_sems += (uint32_t)nsems;
            spinlock_unlock(&g_sem.lock);

            evlog_info(IPC_EV_SOURCE, 1,
                       "semget: created semid=%u key=%d nsems=%u owner=%d",
                       i, key, nsems, s->owner);
            return (int)i;
        }
    }

    spinlock_unlock(&g_sem.lock);
    return -3;
}

int ipc_semop(int semid, const sembuf_t *sops, uint32_t nsops) {
    if (nsops == 0 || !sops) return -1;

    spinlock_lock(&g_sem.lock);
    sem_set_t *s = ipc_sem_find_internal(semid);
    if (!s) {
        spinlock_unlock(&g_sem.lock);
        return -2;
    }

    int nowait = 0;
    int use_undo = 0;
    for (uint32_t i = 0; i < nsops; i++) {
        if (sops[i].sem_flg & SEM_NOWAIT) nowait = 1;
        if (sops[i].sem_flg & SEM_UNDO) use_undo = 1;
    }

retry_check:
    for (uint32_t i = 0; i < nsops; i++) {
        if (sops[i].sem_num >= s->nsems) {
            spinlock_unlock(&g_sem.lock);
            return -3;
        }
        ipc_sem_unit_t *sem = &s->sems[sops[i].sem_num];
        int16_t op = sops[i].sem_op;

        if (op == 0) {
            if (sem->value != 0) {
                if (nowait) {
                    spinlock_unlock(&g_sem.lock);
                    return -1;
                }
                sem->wait_zero++;
                g_sem.stats.total_blocks++;
                spinlock_unlock(&g_sem.lock);
                while (sem->value != 0) { /* 忙等待 */ }
                spinlock_lock(&g_sem.lock);
                sem->wait_zero--;
                g_sem.stats.total_wakeups++;
                goto retry_check;
            }
        } else if (op < 0) {
            if (sem->value + op < 0) {
                if (nowait) {
                    spinlock_unlock(&g_sem.lock);
                    return -1;
                }
                sem->wait_cnt++;
                g_sem.stats.total_blocks++;
                spinlock_unlock(&g_sem.lock);
                while (sem->value + op < 0) { /* 忙等待 */ }
                spinlock_lock(&g_sem.lock);
                sem->wait_cnt--;
                g_sem.stats.total_wakeups++;
                goto retry_check;
            }
        }
    }

    pid_t cur_pid = sched_get_current() ? sched_get_current()->pid : 0;
    for (uint32_t i = 0; i < nsops; i++) {
        ipc_sem_unit_t *sem = &s->sems[sops[i].sem_num];
        sem->value += sops[i].sem_op;
        sem->last_pid = cur_pid;
        if (use_undo && sops[i].sem_op != 0) {
            ipc_sem_undo_record(cur_pid, semid, sops[i].sem_num, sops[i].sem_op);
            g_sem.stats.total_undo_ops++;
        }
    }
    s->otime = (uint32_t)timer_get_ticks();
    g_sem.stats.total_ops++;
    spinlock_unlock(&g_sem.lock);

    return 0;
}

int ipc_semctl(int semid, int semnum, int cmd, void *arg) {
    spinlock_lock(&g_sem.lock);
    sem_set_t *s = ipc_sem_find_internal(semid);
    if (!s) {
        spinlock_unlock(&g_sem.lock);
        return -2;
    }

    int ret = 0;
    switch (cmd) {
        case SEM_GETVAL:
            if (semnum < 0 || (uint32_t)semnum >= s->nsems) { ret = -3; break; }
            ret = s->sems[semnum].value;
            break;

        case SEM_SETVAL:
            if (semnum < 0 || (uint32_t)semnum >= s->nsems || !arg) { ret = -3; break; }
            s->sems[semnum].value = (int16_t)(*(int *)arg);
            s->ctime = (uint32_t)timer_get_ticks();
            break;

        case SEM_GETPID:
            if (semnum < 0 || (uint32_t)semnum >= s->nsems) { ret = -3; break; }
            ret = s->sems[semnum].last_pid;
            break;

        case SEM_GETNCNT:
            if (semnum < 0 || (uint32_t)semnum >= s->nsems) { ret = -3; break; }
            ret = s->sems[semnum].wait_cnt;
            break;

        case SEM_GETZCNT:
            if (semnum < 0 || (uint32_t)semnum >= s->nsems) { ret = -3; break; }
            ret = s->sems[semnum].wait_zero;
            break;

        case SEM_GETALL:
            if (!arg) { ret = -3; break; }
            for (uint32_t i = 0; i < s->nsems; i++) {
                ((uint16_t *)arg)[i] = (uint16_t)s->sems[i].value;
            }
            ret = 0;
            break;

        case SEM_SETALL:
            if (!arg) { ret = -3; break; }
            for (uint32_t i = 0; i < s->nsems; i++) {
                s->sems[i].value = (int16_t)((uint16_t *)arg)[i];
            }
            s->ctime = (uint32_t)timer_get_ticks();
            ret = 0;
            break;

        case SEM_RMID:
            memset(s, 0, sizeof(*s));
            evlog_info(IPC_EV_SOURCE, 2, "sem RMID semid=%d", semid);
            ret = 0;
            break;

        case SEM_STAT:
            if (arg) {
                semid_ds_t *ds = (semid_ds_t *)arg;
                ds->key = s->key;
                ds->nsems = s->nsems;
                ds->owner = s->owner;
                ds->ctime = s->ctime;
                ds->otime = s->otime;
                ds->create_tick = s->create_tick;
            }
            ret = 0;
            break;

        default:
            ret = -4;
            break;
    }

    spinlock_unlock(&g_sem.lock);
    return ret;
}

int ipc_sem_find_by_key(int key) {
    if (key == 0) return -1;
    spinlock_lock(&g_sem.lock);
    sem_set_t *s = ipc_sem_find_by_key_internal(key);
    int ret = s ? (int)(s - g_sem.sets) : -1;
    spinlock_unlock(&g_sem.lock);
    return ret;
}

uint32_t ipc_sem_list(semid_ds_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_sem.lock);
    uint32_t n = 0;
    for (uint32_t i = 0; i < IPC_SEM_MAX_SETS && n < max_count; i++) {
        if (!g_sem.sets[i].in_use) continue;
        semid_ds_t *d = &out[n];
        d->key = g_sem.sets[i].key;
        d->nsems = g_sem.sets[i].nsems;
        d->owner = g_sem.sets[i].owner;
        d->ctime = g_sem.sets[i].ctime;
        d->otime = g_sem.sets[i].otime;
        d->create_tick = g_sem.sets[i].create_tick;
        n++;
    }
    spinlock_unlock(&g_sem.lock);
    return n;
}

void ipc_sem_get_stats(sem_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_sem.lock);
    *stats = g_sem.stats;
    spinlock_unlock(&g_sem.lock);
}

void ipc_sem_reset_stats(void) {
    spinlock_lock(&g_sem.lock);
    memset(&g_sem.stats, 0, sizeof(g_sem.stats));
    spinlock_unlock(&g_sem.lock);
}

void ipc_sem_undo_exit(pid_t pid) {
    spinlock_lock(&g_sem.lock);
    for (uint32_t i = 0; i < g_sem.undo_count; i++) {
        if (g_sem.undo[i].pid != pid) continue;
        sem_set_t *s = ipc_sem_find_internal(g_sem.undo[i].semid);
        if (s && g_sem.undo[i].sem_num < s->nsems) {
            s->sems[g_sem.undo[i].sem_num].value += g_sem.undo[i].adjval;
            evlog_info(IPC_EV_SOURCE, 3,
                       "sem undo exit: semid=%u sem_num=%u adj=%d",
                       g_sem.undo[i].semid, g_sem.undo[i].sem_num,
                       g_sem.undo[i].adjval);
        }
        if (i < g_sem.undo_count - 1) {
            g_sem.undo[i] = g_sem.undo[g_sem.undo_count - 1];
            i--;
        }
        g_sem.undo_count--;
    }
    spinlock_unlock(&g_sem.lock);
}
