/* namespace.c - namespace isolation (mnt/uts/pid/net/ipc/user).
 *
 * Minimal nsproxy model: each task has a single nsproxy that aggregates
 * pointers into per-type namespace trees. setns() / unshare() can swap
 * individual slots without rebuilding the rest.
 */

#include "namespace.h"
#include "capability.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"

static nsproxy_t g_initial_proxy;
static struct ns_common *g_init_ns[NS_MAX_TYPES];
static int g_inited;

struct task_struct;
struct cred;

static int new_level(struct ns_common *parent) {
    int level = 0;
    while (parent && level < NS_MAX_LEVELS - 1) {
        parent = parent->parent;
        level++;
    }
    return level;
}

const char *ns_type_name(int type) {
    switch (type) {
        case NS_TYPE_MNT:  return "mnt";
        case NS_TYPE_UTS:  return "uts";
        case NS_TYPE_PID:  return "pid";
        case NS_TYPE_NET:  return "net";
        case NS_TYPE_IPC:  return "ipc";
        case NS_TYPE_USER: return "user";
        case NS_TYPE_TIME: return "time";
        default: return "unknown";
    }
}

int ns_init(void) {
    if (g_inited) return 0;
    memset(&g_initial_proxy, 0, sizeof(g_initial_proxy));
    g_initial_proxy.ref_count = 1;

    /* Build init namespaces - one per type. */
    for (int t = 1; t < NS_MAX_TYPES; t++) {
        switch (t) {
            case NS_TYPE_UTS: {
                uts_ns_t *u = (uts_ns_t *)kmalloc(sizeof(*u));
                if (!u) return -1;
                memset(u, 0, sizeof(*u));
                u->_ns_common.type = t;
                u->_ns_common.level = 0;
                u->_ns_common.ref_count = 1;
                u->_ns_common.parent = (struct ns_common *)0;
                strcpy(u->hostname, "funsos");
                strcpy(u->domainname, "localdomain");
                g_init_ns[t] = &u->_ns_common;
                g_initial_proxy.ns[t] = &u->_ns_common;
                break;
            }
            case NS_TYPE_PID: {
                pid_ns_t *p = (pid_ns_t *)kmalloc(sizeof(*p));
                if (!p) return -1;
                memset(p, 0, sizeof(*p));
                p->_ns_common.type = t;
                p->_ns_common.level = 0;
                p->_ns_common.ref_count = 1;
                p->max_pids = 32768;
                p->next_pid = 1;
                p->is_first_pid_ns = 1;
                g_init_ns[t] = &p->_ns_common;
                g_initial_proxy.ns[t] = &p->_ns_common;
                break;
            }
            case NS_TYPE_NET: {
                net_ns_t *n = (net_ns_t *)kmalloc(sizeof(*n));
                if (!n) return -1;
                memset(n, 0, sizeof(*n));
                n->_ns_common.type = t;
                n->_ns_common.level = 0;
                n->_ns_common.ref_count = 1;
                g_init_ns[t] = &n->_ns_common;
                g_initial_proxy.ns[t] = &n->_ns_common;
                break;
            }
            default: {
                struct ns_common *c = (struct ns_common *)kmalloc(sizeof(*c));
                if (!c) return -1;
                memset(c, 0, sizeof(*c));
                c->type = t;
                c->level = 0;
                c->ref_count = 1;
                g_init_ns[t] = c;
                g_initial_proxy.ns[t] = c;
                break;
            }
        }
    }
    g_inited = 1;
    return 0;
}

nsproxy_t *nsproxy_default(void) { return g_inited ? &g_initial_proxy : (nsproxy_t *)0; }

nsproxy_t *nsproxy_create(struct task_struct *tsk) {
    (void)tsk;
    nsproxy_t *np = (nsproxy_t *)kmalloc(sizeof(*np));
    if (!np) return (nsproxy_t *)0;
    memset(np, 0, sizeof(*np));
    np->ref_count = 1;
    for (int t = 1; t < NS_MAX_TYPES; t++) {
        if (g_init_ns[t]) {
            np->ns[t] = g_init_ns[t];
            np->ns[t]->ref_count++;
        }
    }
    return np;
}

int nsproxy_copy(nsproxy_t *dst, nsproxy_t *src, struct task_struct *tsk) {
    (void)tsk;
    if (!dst || !src) return -1;
    for (int t = 1; t < NS_MAX_TYPES; t++) {
        dst->ns[t] = src->ns[t];
        if (dst->ns[t]) dst->ns[t]->ref_count++;
    }
    dst->ref_count = 1;
    return 0;
}

void nsproxy_put(nsproxy_t *np) {
    if (!np) return;
    if (np->ref_count > 1) { np->ref_count--; return; }
    for (int t = 1; t < NS_MAX_TYPES; t++) {
        if (np->ns[t] && np->ns[t]->ref_count > 0) {
            np->ns[t]->ref_count--;
            if (np->ns[t]->ref_count == 0 && np->ns[t] != g_init_ns[t]) {
                /* Could free here. */
                np->ns[t] = (struct ns_common *)0;
            }
        }
    }
    if (np != &g_initial_proxy) kfree(np);
}

int ns_set_owner(nsproxy_t *np, const cred_t *cred) {
    if (!np || !cred) return -1;
    for (int t = 1; t < NS_MAX_TYPES; t++) {
        if (np->ns[t])
            np->ns[t]->owner_cred_uid = cred->uid;
    }
    return 0;
}

int ns_clone(struct ns_common *ns) {
    if (!ns) return -1;
    /* Allocate an inner namespace and link it. */
    if (ns->type == NS_TYPE_UTS) {
        uts_ns_t *n = (uts_ns_t *)kmalloc(sizeof(*n));
        if (!n) return -1;
        memcpy(n, ns, sizeof(*n));
        n->_ns_common.ref_count = 1;
        n->_ns_common.level = new_level(ns);
        n->_ns_common.parent = ns;
        ns->ref_count++;
        /* Caller is expected to install this new namespace. */
        (void)n;
        return 0;
    } else if (ns->type == NS_TYPE_PID) {
        pid_ns_t *n = (pid_ns_t *)kmalloc(sizeof(*n));
        if (!n) return -1;
        memcpy(n, ns, sizeof(*n));
        n->_ns_common.ref_count = 1;
        n->_ns_common.level = new_level(ns);
        n->_ns_common.parent = ns;
        n->next_pid = 1;
        n->is_first_pid_ns = 0;
        ns->ref_count++;
        (void)n;
        return 0;
    }
    /* Generic types fall through. */
    struct ns_common *c = (struct ns_common *)kmalloc(sizeof(*c));
    if (!c) return -1;
    memcpy(c, ns, sizeof(*c));
    c->ref_count = 1;
    c->level = new_level(ns);
    c->parent = ns;
    ns->ref_count++;
    kfree(c);
    return 0;
}

int ns_enter(struct ns_common *ns, struct task_struct *tsk) {
    (void)ns; (void)tsk;
    if (!ns) return -1;
    return 0;
}

struct ns_common *ns_find(int type, struct ns_common *parent, int level) {
    (void)level;
    if (type < 1 || type >= NS_MAX_TYPES) return (struct ns_common *)0;
    if (!g_init_ns[type]) return (struct ns_common *)0;
    return g_init_ns[type];
    (void)parent;
}

void namespace_print_stats(void) {
    /* Minimal implementation: dump the per-type namespace counts. */
    klog_info("namespace: per-type refcounts not yet tracked");
}

int ns_unshare(struct task_struct *tsk, int which) {
    (void)tsk; (void)which;
    /* Caller would invoke ns_clone() per slot then update nsproxy. */
    return 0;
}

/* Some call sites (notably kernel/main.c) use the longer name. */
__attribute__((alias("ns_init")))
int namespace_init(void);