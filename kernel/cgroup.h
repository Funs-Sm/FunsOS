#ifndef CGROUP_H
#define CGROUP_H

#include "stdint.h"
#include "kernel_types.h"

/* ============================================================
 * Cgroup (Control Groups) 控制组子系统
 *
 * 提供进程资源限制和隔离机制。
 * 支持 CPU、内存、I/O 等子系统资源控制。
 * ============================================================ */

/* 最大 cgroup 数量 */
#define CGROUP_MAX_GROUPS      64
#define CGROUP_MAX_PROCS      256
#define CGROUP_NAME_MAX        64
#define CGROUP_SUBSYS_MAX       8

/* 子系统类型 */
typedef enum {
    CGROUP_SUBSYS_CPU = 0,      /* CPU 调度控制 */
    CGROUP_SUBSYS_MEM,          /* 内存限制 */
    CGROUP_SUBSYS_IO,           /* I/O 限制 */
    CGROUP_SUBSYS_CPUSET,       /* CPU 亲和性 */
    CGROUP_SUBSYS_FREEZER,      /* 进程冻结 */
    CGROUP_SUBSYS_NET,          /* 网络带宽限制 */
    CGROUP_SUBSYS_DEVICE,       /* 设备访问控制 */
    CGROUP_SUBSYS_BLKIO,        /* 块 I/O 控制 */
} cgroup_subsys_t;

/* Cgroup 状态 */
typedef enum {
    CGROUP_STATE_INACTIVE = 0,
    CGROUP_STATE_ACTIVE,
    CGROUP_STATE_FROZEN,
    CGROUP_STATE_DELETING,
} cgroup_state_t;

/* CPU 子系统参数 */
typedef struct {
    uint32_t shares;            /* CPU 份额 (默认 1024) */
    uint32_t quota_us;          /* CPU 配额 (微秒/周期), -1 表示无限制 */
    uint32_t period_us;         /* CPU 周期 (微秒, 默认 100000) */
    uint32_t rt_runtime_us;     /* 实时运行时间 */
    uint32_t rt_period_us;      /* 实时周期 */
    uint64_t cpu_usage;         /* CPU 使用量 (纳秒) */
} cgroup_cpu_t;

/* 内存子系统参数 */
typedef struct {
    uint64_t limit_in_bytes;    /* 内存限制 (字节) */
    uint64_t soft_limit;        /* 软限制 */
    uint64_t usage_in_bytes;    /* 当前使用量 */
    uint64_t max_usage;         /* 历史峰值 */
    uint64_t failcnt;           /* 失败计数 */
    uint32_t oom_control;       /* OOM 控制 */
    uint64_t swap_limit;        /* 交换区限制 */
} cgroup_mem_t;

/* I/O 子系统参数 */
typedef struct {
    uint64_t read_bps;          /* 读速率 (字节/秒) */
    uint64_t write_bps;         /* 写速率 (字节/秒) */
    uint32_t read_iops;         /* 读 IOPS */
    uint32_t write_iops;        /* 写 IOPS */
    uint64_t total_read_bytes;  /* 总读字节 */
    uint64_t total_write_bytes; /* 总写字节 */
} cgroup_io_t;

/* cpuset 子系统参数 */
typedef struct {
    uint32_t cpumask;           /* CPU 掩码 */
    uint32_t mems_allowed;      /* 允许的内存节点 */
    int memory_migrate;         /* 内存迁移 */
    int cpu_exclusive;          /* CPU 独占 */
    int mem_exclusive;          /* 内存独占 */
} cgroup_cpuset_t;

/* freezer 子系统参数 */
typedef struct {
    int state;                  /* 冻结状态: 0=运行, 1=冻结中, 2=已冻结 */
    uint32_t frozen_count;      /* 已冻结进程数 */
} cgroup_freezer_t;

/* 网络子系统参数 */
typedef struct {
    uint32_t max_rate;          /* 最大速率 (kbps) */
    uint32_t priority;          /* 优先级 */
    uint64_t tx_bytes;          /* 发送字节数 */
    uint64_t rx_bytes;          /* 接收字节数 */
} cgroup_net_t;

/* Cgroup 结构 */
typedef struct cgroup {
    char                name[CGROUP_NAME_MAX];
    int                 id;
    int                 used;
    cgroup_state_t      state;
    uint32_t            subsys_mask;    /* 启用的子系统掩码 */
    int                 parent_id;      /* 父 cgroup ID */

    /* 进程列表 */
    pid_t               procs[CGROUP_MAX_PROCS];
    uint32_t            proc_count;

    /* 子系统参数 */
    cgroup_cpu_t        cpu;
    cgroup_mem_t        mem;
    cgroup_io_t         io;
    cgroup_cpuset_t     cpuset;
    cgroup_freezer_t    freezer;
    cgroup_net_t        net;

    /* 统计 */
    uint64_t            created_time;
    uint64_t            last_updated;
} cgroup_t;

/* ============================================================
 * 初始化
 * ============================================================ */
void cgroup_init(void);

/* ============================================================
 * Cgroup 管理
 * ============================================================ */

/*
 * cgroup_create - 创建 cgroup
 * name: cgroup 名称
 * subsys_mask: 子系统掩码
 * parent_id: 父 cgroup ID (-1 表示根)
 * 返回: cgroup ID, 负数错误码
 */
int cgroup_create(const char *name, uint32_t subsys_mask, int parent_id);

/*
 * cgroup_delete - 删除 cgroup
 * id: cgroup ID
 * 返回: 0 成功, 负数错误码
 */
int cgroup_delete(int id);

/*
 * cgroup_attach - 将进程加入 cgroup
 * id: cgroup ID
 * pid: 进程 PID
 * 返回: 0 成功, 负数错误码
 */
int cgroup_attach(int id, pid_t pid);

/*
 * cgroup_detach - 将进程移出 cgroup
 * id: cgroup ID
 * pid: 进程 PID
 * 返回: 0 成功, 负数错误码
 */
int cgroup_detach(int id, pid_t pid);

/* ============================================================
 * 参数设置
 * ============================================================ */

int cgroup_set_cpu_shares(int id, uint32_t shares);
int cgroup_set_cpu_quota(int id, uint32_t quota_us);
int cgroup_set_mem_limit(int id, uint64_t limit_bytes);
int cgroup_set_io_read_bps(int id, uint64_t bps);
int cgroup_set_freeze(int id, int freeze);
int cgroup_set_cpumask(int id, uint32_t cpumask);

/* ============================================================
 * 查询接口
 * ============================================================ */

cgroup_t *cgroup_get(int id);
cgroup_t *cgroup_find_by_name(const char *name);
int cgroup_get_proc_cgroup(pid_t pid);
void cgroup_dump(int id);

/* ============================================================
 * 系统调用接口
 * ============================================================ */
int sys_cgroup_create(const char *name, uint32_t subsys_mask, int parent_id);
int sys_cgroup_delete(int id);
int sys_cgroup_attach(int id, pid_t pid);
int sys_cgroup_detach(int id, pid_t pid);

#endif /* CGROUP_H */
