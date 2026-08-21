#ifndef SECURITY_HOOK_H
#define SECURITY_HOOK_H

#include "stdint.h"

/* Linux Security Module (LSM) compatible security hook framework.
 *
 * Provides a small set of decision points that any LSM can plug into
 * via a registration structure. The default dispositioning policy is
 * "permissive" so that without an active module, the framework is a
 * pass-through that introduces no behaviour change.
 *
 * The framework coexists with capability and Discretionary Access
 * Control (DAC): DAC decisions are taken first, then the security hook
 * is consulted for an additional verdict.
 */

#include "capability.h"

#define LSM_MAX_HOOKS            32
#define LSM_MAX_MODULES          8

/* Hook IDs */
enum {
    LSM_HOOK_INODE_ALLOC        = 0,
    LSM_HOOK_INODE_FREE         = 1,
    LSM_HOOK_INODE_PERMISSION   = 2,
    LSM_HOOK_FILE_OPEN          = 3,
    LSM_HOOK_TASK_ALLOC         = 4,
    LSM_HOOK_TASK_FREE          = 5,
    LSM_HOOK_TASK_KILL          = 6,
    LSM_HOOK_MOUNT              = 7,
    LSM_HOOK_NETIF              = 8,
    LSM_HOOK_CAP_CHECK          = 9,
    LSM_HOOK_BPRM_TRANSITION    = 10,
    LSM_HOOK_BPF                = 11,
    LSM_HOOK_SYSLOG             = 12,
    LSM_HOOK_AUDIT              = 13,
};

/* Decisions */
#define LSM_DENY               (-1)
#define LSM_GRANT               0

/* Hook function signatures. */
typedef int (*lsm_inode_alloc_t)(void *inode, uint32_t cred_uid);
typedef int (*lsm_inode_free_t)(void *inode);
typedef int (*lsm_inode_permission_t)(void *inode, uint32_t cred_uid,
                                       uint32_t requested);
typedef int (*lsm_file_open_t)(void *file, uint32_t cred_uid);
typedef int (*lsm_task_alloc_t)(void *task, const cred_t *cred);
typedef int (*lsm_task_free_t)(void *task);
typedef int (*lsm_task_kill_t)(void *task, void *target);
typedef int (*lsm_mount_t)(const char *src, const char *target);
typedef int (*lsm_netif_t)(const char *name, int up);
typedef int (*lsm_cap_check_t)(const cred_t *cred, int cap, int effective);
typedef int (*lsm_bprm_transition_t)(const cred_t *old_cred,
                                       const cred_t *new_cred);
typedef int (*lsm_bpf_t)(int cmd);
typedef int (*lsm_syslog_t)(int type);

/* A module bundles pointers to optional handlers. NULL = no-op. */
typedef struct lsm_module {
    const char *name;
    uint8_t     enabled;
    uint32_t    flags;
    lsm_inode_alloc_t           inode_alloc;
    lsm_inode_free_t            inode_free;
    lsm_inode_permission_t      inode_permission;
    lsm_file_open_t             file_open;
    lsm_task_alloc_t            task_alloc;
    lsm_task_free_t             task_free;
    lsm_task_kill_t             task_kill;
    lsm_mount_t                 mount;
    lsm_netif_t                 netif;
    lsm_cap_check_t             cap_check;
    lsm_bprm_transition_t      bprm_transition;
    lsm_bpf_t                   bpf;
    lsm_syslog_t                syslog;
    struct lsm_module *next;
} lsm_module_t;

int lsm_register(lsm_module_t *m);
int lsm_unregister(lsm_module_t *m);
int lsm_enable(const char *name);
int lsm_disable(const char *name);

/* Hook invocation API (called by the rest of the kernel). */
static inline int lsm_inode_permission(void *inode, uint32_t uid, uint32_t req) {
    extern int lsm_inode_permission_run(void *inode, uint32_t uid, uint32_t req);
    return lsm_inode_permission_run(inode, uid, req);
}
int lsm_inode_permission_run(void *inode, uint32_t uid, uint32_t req);
int lsm_task_alloc_run(void *task, const cred_t *cred);
int lsm_task_free_run(void *task);
int lsm_cap_check_run(const cred_t *cred, int cap, int effective);
int lsm_mount_run(const char *src, const char *target);

#endif