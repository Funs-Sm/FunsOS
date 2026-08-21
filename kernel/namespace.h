#ifndef NS_NS_H
#define NS_NS_H

#include "stdint.h"
#include "capability.h"   /* for cred_t */

/* Namespace isolation.
 *
 * Per-task namespaces decouple the view of a particular global resource
 * so different tasks see different instances. Each namespace has
 * refcounts and methods to enter / leave.
 *
 * Supported types (subset):
 *   - MNT  : Mount namespace (filesystem hierarchy)
 *   - UTS  : Hostname / domain
 *   - PID  : Process ID space
 *   - NET  : Network stack
 *   - IPC  : SysV IPC and Posix mqueue
 *   - USER : User/group ID mappings
 *   - TIME : Clock/boottime view
 */

#define NS_TYPE_MNT      1
#define NS_TYPE_UTS      2
#define NS_TYPE_PID      3
#define NS_TYPE_NET      4
#define NS_TYPE_IPC      5
#define NS_TYPE_USER     6
#define NS_TYPE_TIME     7

#define NS_MAX_TYPES     8
#define NS_MAX_LEVELS    64

struct ns_common {
    int   type;
    int   level;
    uint32_t ref_count;
    uint64_t owner_cred_uid;
    struct ns_common *parent;
};

/* Each namespace type embeds this struct at the start. */
#define NS_COMMON \
    struct ns_common _ns_common;

typedef struct {
    NS_COMMON
    /* Mount-specific state. */
    char hostname[64];
    char domainname[64];
    /* Number of mounts visible inside. */
    uint32_t mount_count;
} uts_ns_t;

typedef struct {
    NS_COMMON
    uint32_t max_pids;
    uint32_t active_pids;
    uint32_t next_pid;
    int      is_first_pid_ns;
} pid_ns_t;

typedef struct {
    NS_COMMON
    void *net_state;
} net_ns_t;

/* nsproxy - holder of all namespaces for a task. */
typedef struct nsproxy {
    struct ns_common *ns[NS_MAX_TYPES];
    uint32_t ref_count;
} nsproxy_t;

struct task_struct;
struct cred;

int            ns_init(void);
int            namespace_init(void);
nsproxy_t     *nsproxy_create(struct task_struct *tsk);
int            nsproxy_copy(nsproxy_t *dst, nsproxy_t *src, struct task_struct *tsk);
void           nsproxy_put(nsproxy_t *np);
int            ns_set_owner(nsproxy_t *np, const cred_t *cred);

int            ns_enter(struct ns_common *ns, struct task_struct *tsk);
int            ns_clone(struct ns_common *ns);
int            ns_unshare(struct task_struct *tsk, int which);
struct ns_common *ns_find(int type, struct ns_common *parent, int level);
void           namespace_print_stats(void);

nsproxy_t     *nsproxy_default(void);
struct ns_common *ns_find(int type, struct ns_common *parent, int level);

const char    *ns_type_name(int type);

#endif