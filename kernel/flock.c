#include "flock.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "sync.h"

#define LOCK_HASH_SIZE  64  /* 哈希桶数量 */
#define MAX_LOCKS      256  /* 最大锁数量 */

static lock_record_t *g_lock_table[LOCK_HASH_SIZE];
static uint32_t g_lock_count = 0;
static spinlock_t g_lock;
static lock_stats_t g_stats;
static int g_initialized = 0;

/* ---- 哈希函数 ---- */
static inline uint32_t lock_hash(uint32_t dev, uint32_t inode) {
    return ((dev * 31) + inode) % LOCK_HASH_SIZE;
}

/* ---- 范围检查 ---- */
static inline int ranges_overlap(uint32_t s1, uint32_t e1,
                                  uint32_t s2, uint32_t e2) {
    return (s1 <= e2) && (s2 <= e1);
}

/* ---- 检查锁冲突 ----
 * 检查给定的锁请求是否与现有锁冲突
 * 返回冲突的锁记录，没有冲突返回 NULL */
static lock_record_t *find_conflict(uint32_t dev, uint32_t inode,
                                     uint16_t type, uint32_t start,
                                     uint32_t end, uint32_t pid) {
    uint32_t h = lock_hash(dev, inode);
    lock_record_t *lr = g_lock_table[h];

    while (lr) {
        if (lr->dev == dev && lr->inode == inode && lr->pid != pid) {
            if (ranges_overlap(lr->start, lr->end, start, end)) {
                /* 读锁只与写锁冲突
                 * 写锁与读锁和写锁都冲突 */
                if (type == F_RDLCK) {
                    if (lr->type == F_WRLCK) {
                        return lr;
                    }
                } else { /* F_WRLCK */
                    return lr;  /* 写锁与任何锁都冲突 */
                }
            }
        }
        lr = lr->next;
    }
    return NULL;
}

/* ---- 查找指定进程的锁 ---- */
static lock_record_t *find_pid_lock(uint32_t dev, uint32_t inode,
                                     uint32_t pid, uint32_t start,
                                     uint32_t end) {
    uint32_t h = lock_hash(dev, inode);
    lock_record_t *lr = g_lock_table[h];

    while (lr) {
        if (lr->dev == dev && lr->inode == inode && lr->pid == pid &&
            lr->start == start && lr->end == end) {
            return lr;
        }
        lr = lr->next;
    }
    return NULL;
}

/* ---- 插入锁记录 ---- */
static int insert_lock(uint32_t dev, uint32_t inode, uint16_t type,
                       uint32_t start, uint32_t end, uint32_t pid) {
    if (g_lock_count >= MAX_LOCKS) {
        return -1;
    }

    lock_record_t *lr = kmalloc(sizeof(lock_record_t));
    if (!lr) return -12; /* ENOMEM */

    lr->dev = dev;
    lr->inode = inode;
    lr->type = type;
    lr->start = start;
    lr->end = end;
    lr->pid = pid;
    lr->next = NULL;
    lr->prev = NULL;

    uint32_t h = lock_hash(dev, inode);
    if (g_lock_table[h]) {
        g_lock_table[h]->prev = lr;
        lr->next = g_lock_table[h];
    }
    g_lock_table[h] = lr;

    g_lock_count++;
    if (g_lock_count > g_stats.max_locks) {
        g_stats.max_locks = g_lock_count;
    }

    g_stats.lock_acquires++;
    if (type == F_RDLCK) g_stats.read_locks++;
    else if (type == F_WRLCK) g_stats.write_locks++;
    g_stats.total_locks = g_lock_count;

    return 0;
}

/* ---- 删除锁记录 ---- */
static void remove_lock(lock_record_t *lr) {
    uint32_t h = lock_hash(lr->dev, lr->inode);

    if (lr->prev) {
        lr->prev->next = lr->next;
    } else {
        g_lock_table[h] = lr->next;
    }
    if (lr->next) {
        lr->next->prev = lr->prev;
    }

    if (lr->type == F_RDLCK && g_stats.read_locks > 0) g_stats.read_locks--;
    else if (lr->type == F_WRLCK && g_stats.write_locks > 0) g_stats.write_locks--;
    g_stats.lock_releases++;
    g_lock_count--;
    g_stats.total_locks = g_lock_count;

    kfree(lr);
}

/* ---- 初始化 ---- */
void flock_init(void) {
    for (int i = 0; i < LOCK_HASH_SIZE; i++) {
        g_lock_table[i] = NULL;
    }
    g_lock_count = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    spinlock_init(&g_lock);
    g_initialized = 1;
    klog_info("flock: file locking subsystem initialized (max %d locks)", MAX_LOCKS);
}

/* ---- flock_get - 检查是否有冲突锁 ---- */
int flock_get(uint32_t dev, uint32_t inode, file_lock_t *flock) {
    if (!g_initialized) flock_init();
    if (!flock) return -22; /* EINVAL */

    if (flock->l_type == F_UNLCK) {
        /* UNLCK 总是 "可用" */
        return 0;
    }

    uint32_t start = flock->l_start;
    uint32_t len = flock->l_len;
    uint32_t end = (len == 0) ? 0xFFFFFFFF : (start + len - 1);

    spinlock_lock(&g_lock);

    lock_record_t *conflict = find_conflict(dev, inode, flock->l_type,
                                             start, end, flock->l_pid);

    if (conflict) {
        /* 填充冲突锁信息 */
        flock->l_type = conflict->type;
        flock->l_whence = SEEK_SET;
        flock->l_start = conflict->start;
        flock->l_len = conflict->end - conflict->start + 1;
        flock->l_pid = conflict->pid;
        g_stats.lock_conflicts++;
        spinlock_unlock(&g_lock);
        return 1;
    }

    spinlock_unlock(&g_lock);
    return 0;
}

/* ---- flock_set - 设置锁（非阻塞）---- */
int flock_set(uint32_t dev, uint32_t inode, const file_lock_t *flock, uint32_t pid) {
    if (!g_initialized) flock_init();
    if (!flock) return -22; /* EINVAL */

    uint32_t start = flock->l_start;
    uint32_t len = flock->l_len;
    uint32_t end = (len == 0) ? 0xFFFFFFFF : (start + len - 1);

    spinlock_lock(&g_lock);

    if (flock->l_type == F_UNLCK) {
        /* 解锁：删除指定范围的锁 */
        lock_record_t *lr = g_lock_table[lock_hash(dev, inode)];
        int found = 0;
        while (lr) {
            lock_record_t *next = lr->next;
            if (lr->dev == dev && lr->inode == inode && lr->pid == pid) {
                /* 简化：只要有重叠就删除整个已存在的锁（简单实现）*/
                uint32_t min_end = lr->end < end ? lr->end : end;
                uint32_t max_start = lr->start > start ? lr->start : start;
                if (max_start <= min_end) {
                    remove_lock(lr);
                    found = 1;
                }
            }
            lr = next;
        }
        spinlock_unlock(&g_lock);
        return found ? 0 : -2; /* ENOENT */
    }

    /* 检查冲突 */
    lock_record_t *conflict = find_conflict(dev, inode, flock->l_type,
                                             start, end, pid);
    if (conflict) {
        g_stats.lock_conflicts++;
        spinlock_unlock(&g_lock);
        return -1; /* EAGAIN/EWOULDBLOCK */
    }

    /* 尝试合并已有的同类型锁（简化：先删除再插入）*/
    lock_record_t *lr = g_lock_table[lock_hash(dev, inode)];
    while (lr) {
        lock_record_t *next = lr->next;
        if (lr->dev == dev && lr->inode == inode &&
            lr->pid == pid && lr->type == flock->l_type) {
            /* 如果相邻或重叠，合并 */
            uint32_t new_end = lr->end > end ? lr->end : end;
            uint32_t new_start = lr->start < start ? lr->start : start;
            uint32_t total_len = new_end - new_start + 1;
            /* 如果有重叠或相邻 */
            if ((start <= lr->end + 1) && (end + 1 >= lr->start)) {
                remove_lock(lr);
                /* 继续搜索，可能需要合并多个 */
                start = new_start;
                end = new_end;
                lr = g_lock_table[lock_hash(dev, inode)];
                continue;
            }
        }
        lr = next;
    }

    /* 插入新锁 */
    int ret = insert_lock(dev, inode, flock->l_type, start, end, pid);
    spinlock_unlock(&g_lock);
    return ret;
}

/* ---- flock_set_wait - 设置锁（阻塞等待）---- */
int flock_set_wait(uint32_t dev, uint32_t inode, const file_lock_t *flock,
                   uint32_t pid) {
    if (!g_initialized) flock_init();
    if (!flock) return -22;

    g_stats.lock_waits++;

    /* 简化实现：最多重试 100 次，每次短延迟 */
    for (int i = 0; i < 100; i++) {
        int ret = flock_set(dev, inode, flock, pid);
        if (ret != -1) return ret;  /* 成功或其他错误 */
        /* 简单忙等（实际系统应使用等待队列）*/
        for (volatile int j = 0; j < 10000; j++) {}
    }

    return -1; /* 超时 */
}

/* ---- 释放指定进程在指定 inode 上的所有锁 ---- */
int flock_release_all(uint32_t dev, uint32_t inode, uint32_t pid) {
    if (!g_initialized) return -1;

    spinlock_lock(&g_lock);

    uint32_t h = lock_hash(dev, inode);
    lock_record_t *lr = g_lock_table[h];
    int count = 0;

    while (lr) {
        lock_record_t *next = lr->next;
        if (lr->dev == dev && lr->inode == inode && lr->pid == pid) {
            remove_lock(lr);
            count++;
        }
        lr = next;
    }

    spinlock_unlock(&g_lock);
    return count;
}

/* ---- 释放指定进程的所有锁 ---- */
void flock_release_pid(uint32_t pid) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);

    for (int i = 0; i < LOCK_HASH_SIZE; i++) {
        lock_record_t *lr = g_lock_table[i];
        while (lr) {
            lock_record_t *next = lr->next;
            if (lr->pid == pid) {
                remove_lock(lr);
            }
            lr = next;
        }
    }

    spinlock_unlock(&g_lock);
}

/* ---- 统计 ---- */
void flock_get_stats(lock_stats_t *stats) {
    if (!stats) return;
    if (!g_initialized) flock_init();

    spinlock_lock(&g_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_lock);
}

void flock_reset_stats(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    uint32_t saved_total = g_stats.total_locks;
    uint32_t saved_read = g_stats.read_locks;
    uint32_t saved_write = g_stats.write_locks;
    uint32_t saved_max = g_stats.max_locks;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.total_locks = saved_total;
    g_stats.read_locks = saved_read;
    g_stats.write_locks = saved_write;
    g_stats.max_locks = saved_max;
    spinlock_unlock(&g_lock);
}

/* ---- 列出锁（调试用）---- */
int flock_list(uint32_t dev, uint32_t inode, file_lock_t *locks,
               int max_locks) {
    if (!locks || max_locks <= 0) return -22;
    if (!g_initialized) flock_init();

    spinlock_lock(&g_lock);

    uint32_t h = lock_hash(dev, inode);
    lock_record_t *lr = g_lock_table[h];
    int count = 0;

    while (lr && count < max_locks) {
        if (lr->dev == dev && lr->inode == inode) {
            locks[count].l_type = lr->type;
            locks[count].l_whence = SEEK_SET;
            locks[count].l_start = lr->start;
            locks[count].l_len = lr->end - lr->start + 1;
            locks[count].l_pid = lr->pid;
            count++;
        }
        lr = lr->next;
    }

    spinlock_unlock(&g_lock);
    return count;
}
