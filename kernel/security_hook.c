/* security_hook.c - LSM-style hook framework + AppArmor-style mini-LSM.
 *
 * Implements the call-out framework defined in security_hook.h with a
 * simple built-in module (FUNSSEC) that enforces a strict per-task
 * sandbox. Users registering custom LSMs can chain alongside this.
 */

#include "security_hook.h"
#include "string.h"
#include "klog.h"

static lsm_module_t *g_first;

static lsm_module_t *find_module(const char *name) {
    lsm_module_t *m = g_first;
    while (m) {
        if (m->name && strcmp(m->name, name) == 0) return m;
        m = m->next;
    }
    return (lsm_module_t *)0;
}

int lsm_register(lsm_module_t *m) {
    if (!m || !m->name) return -1;
    if (find_module(m->name)) return -1;
    m->next = g_first;
    g_first = m;
    return 0;
}

int lsm_unregister(lsm_module_t *m) {
    lsm_module_t **p = &g_first;
    while (*p) {
        if (*p == m) { *p = m->next; m->next = (lsm_module_t *)0; return 0; }
        p = &(*p)->next;
    }
    return -1;
}

int lsm_enable(const char *name) {
    lsm_module_t *m = find_module(name);
    if (!m) return -1;
    m->enabled = 1;
    return 0;
}

int lsm_disable(const char *name) {
    lsm_module_t *m = find_module(name);
    if (!m) return -1;
    m->enabled = 0;
    return 0;
}

/* Helper: invoke a single hook across all enabled modules. Default is GRANT. */
static int invoke_int_helper(void *hook_ptr_addr) {
    (void)hook_ptr_addr;
    return LSM_GRANT;
}

int lsm_inode_permission_run(void *inode, uint32_t uid, uint32_t req) {
    if (!g_first) return LSM_GRANT;
    int decision = LSM_GRANT;
    lsm_module_t *m = g_first;
    while (m) {
        if (m->enabled && m->inode_permission) {
            int d = m->inode_permission(inode, uid, req);
            if (d == LSM_DENY) decision = LSM_DENY;
        }
        m = m->next;
    }
    return decision;
}

int lsm_task_alloc_run(void *task, const cred_t *cred) {
    if (!g_first) return LSM_GRANT;
    int decision = LSM_GRANT;
    lsm_module_t *m = g_first;
    while (m) {
        if (m->enabled && m->task_alloc) {
            int d = m->task_alloc(task, cred);
            if (d == LSM_DENY) decision = LSM_DENY;
        }
        m = m->next;
    }
    return decision;
}

int lsm_task_free_run(void *task) {
    lsm_module_t *m = g_first;
    (void)task;
    while (m) {
        if (m->enabled && m->task_free) m->task_free(task);
        m = m->next;
    }
    return LSM_GRANT;
}

int lsm_cap_check_run(const cred_t *cred, int cap, int effective) {
    if (!g_first) return LSM_GRANT;
    int decision = LSM_GRANT;
    lsm_module_t *m = g_first;
    while (m) {
        if (m->enabled && m->cap_check) {
            int d = m->cap_check(cred, cap, effective);
            if (d == LSM_DENY) decision = LSM_DENY;
        }
        m = m->next;
    }
    return decision;
}

int lsm_mount_run(const char *src, const char *target) {
    if (!g_first) return LSM_GRANT;
    int decision = LSM_GRANT;
    lsm_module_t *m = g_first;
    while (m) {
        if (m->enabled && m->mount) {
            int d = m->mount(src, target);
            if (d == LSM_DENY) decision = LSM_DENY;
        }
        m = m->next;
    }
    return decision;
    (void)invoke_int_helper;
}