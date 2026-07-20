#ifndef NAMESPACE_H
#define NAMESPACE_H

#include "stdint.h"
#include "kernel_types.h"

/* ============================================================
 * Namespaces 命名空间子系统
 *
 * 提供进程资源隔离机制，支持六种命名空间：
 *   - PID:    进程ID隔离
 *   - Mount:  文件系统挂载点隔离
 *   - Net:    网络栈隔离 (复用netns)
 *   - UTS:    主机名/域名隔离
 *   - IPC:    System V IPC隔离
 *   - User:   用户/组ID隔离
 * ============================================================ */

#define NAMESPACE_MAX_PROXIES    32
#define NAMESPACE_UTSNAME_MAX    65
#define NAMESPACE_MAX_MOUNTS     16
#define NAMESPACE_MAX_PIDMAPS    16

struct net;
struct uts_namespace;
struct ipc_namespace;
struct mnt_namespace;
struct pid_namespace;
struct user_namespace;

/* UTS 命名空间 (UNIX Time-sharing System) */
typedef struct uts_namespace {
    int    used;
    uint32_t refcount;
    char   sysname[NAMESPACE_UTSNAME_MAX];
    char   nodename[NAMESPACE_UTSNAME_MAX];
    char   domainname[NAMESPACE_UTSNAME_MAX];
    char   release[NAMESPACE_UTSNAME_MAX];
    char   version[NAMESPACE_UTSNAME_MAX];
    char   machine[NAMESPACE_UTSNAME_MAX];
} uts_ns_t;

/* PID 命名空间 */
typedef struct pid_namespace {
    int    used;
    uint32_t refcount;
    uint32_t level;
    pid_t  last_pid;
    uint32_t pid_allocated;
    struct pid_namespace *parent;
} pid_ns_t;

/* Mount 命名空间 */
typedef struct mnt_namespace {
    int    used;
    uint32_t refcount;
    uint32_t mount_count;
    uint32_t root_mnt;
} mnt_ns_t;

/* IPC 命名空间 */
typedef struct ipc_namespace {
    int    used;
    uint32_t refcount;
    uint32_t sem_ids;
    uint32_t msg_ids;
    uint32_t shm_ids;
} ipc_ns_t;

/* User 命名空间 */
typedef struct user_namespace {
    int    used;
    uint32_t refcount;
    uint32_t level;
    uint32_t owner;
    struct user_namespace *parent;
} user_ns_t;

/* Namespace proxy - 一个进程持有的命名空间集合 */
typedef struct nsproxy {
    int              used;
    uint32_t         refcount;
    pid_ns_t        *pid_ns;
    mnt_ns_t        *mnt_ns;
    struct net      *net_ns;
    uts_ns_t        *uts_ns;
    ipc_ns_t        *ipc_ns;
    user_ns_t       *user_ns;
    uint64_t         created_time;
} nsproxy_t;

/* ============================================================
 * 初始化
 * ============================================================ */
int namespace_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

nsproxy_t *nsproxy_get(void);
int copy_namespaces(unsigned long flags, nsproxy_t *old, nsproxy_t **newp);
int create_new_namespace(unsigned long flags, nsproxy_t **newp);
void get_nsproxy(nsproxy_t *ns);
void put_nsproxy(nsproxy_t *ns);

uts_ns_t *uts_ns_create(void);
pid_ns_t *pid_ns_create(pid_ns_t *parent);
mnt_ns_t *mnt_ns_create(void);
ipc_ns_t *ipc_ns_create(void);
user_ns_t *user_ns_create(user_ns_t *parent, uint32_t owner);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void namespace_print_stats(void);

#endif /* NAMESPACE_H */
