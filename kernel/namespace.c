#include "namespace.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"
#include "netns.h"

#define CLONE_NEWNS    0x00020000
#define CLONE_NEWUTS   0x04000000
#define CLONE_NEWIPC   0x08000000
#define CLONE_NEWUSER  0x10000000
#define CLONE_NEWPID   0x20000000
#define CLONE_NEWNET   0x40000000

struct namespace_global {
    uint8_t     initialized;
    nsproxy_t   proxies[NAMESPACE_MAX_PROXIES];
    uint32_t    proxy_count;
    nsproxy_t  *init_proxy;

    uts_ns_t    uts_cache[NAMESPACE_MAX_PROXIES];
    uint32_t    uts_count;
    pid_ns_t    pid_cache[NAMESPACE_MAX_PROXIES];
    uint32_t    pid_count;
    mnt_ns_t    mnt_cache[NAMESPACE_MAX_PROXIES];
    uint32_t    mnt_count;
    ipc_ns_t    ipc_cache[NAMESPACE_MAX_PROXIES];
    uint32_t    ipc_count;
    user_ns_t   user_cache[NAMESPACE_MAX_PROXIES];
    uint32_t    user_count;

    uint64_t    total_gets;
    uint64_t    total_puts;
    uint64_t    total_copies;
    uint64_t    total_creates;
};

static struct namespace_global ns_data;

static uts_ns_t *ns_alloc_uts(void) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.uts_cache[i].used) {
            uts_ns_t *uts = &ns_data.uts_cache[i];
            memset(uts, 0, sizeof(*uts));
            uts->used = 1;
            uts->refcount = 1;
            strncpy(uts->sysname, "FunsOS", NAMESPACE_UTSNAME_MAX - 1);
            strncpy(uts->nodename, "funsos", NAMESPACE_UTSNAME_MAX - 1);
            strncpy(uts->release, "0.5.0", NAMESPACE_UTSNAME_MAX - 1);
            strncpy(uts->version, "#1 SMP", NAMESPACE_UTSNAME_MAX - 1);
            strncpy(uts->machine, "i686", NAMESPACE_UTSNAME_MAX - 1);
            strncpy(uts->domainname, "(none)", NAMESPACE_UTSNAME_MAX - 1);
            ns_data.uts_count++;
            return uts;
        }
    }
    return NULL;
}

static pid_ns_t *ns_alloc_pid(pid_ns_t *parent) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.pid_cache[i].used) {
            pid_ns_t *pidns = &ns_data.pid_cache[i];
            memset(pidns, 0, sizeof(*pidns));
            pidns->used = 1;
            pidns->refcount = 1;
            pidns->parent = parent;
            pidns->level = parent ? parent->level + 1 : 0;
            pidns->last_pid = 0;
            pidns->pid_allocated = 0;
            ns_data.pid_count++;
            return pidns;
        }
    }
    return NULL;
}

static mnt_ns_t *ns_alloc_mnt(void) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.mnt_cache[i].used) {
            mnt_ns_t *mnt = &ns_data.mnt_cache[i];
            memset(mnt, 0, sizeof(*mnt));
            mnt->used = 1;
            mnt->refcount = 1;
            mnt->mount_count = 4;
            mnt->root_mnt = 1;
            ns_data.mnt_count++;
            return mnt;
        }
    }
    return NULL;
}

static ipc_ns_t *ns_alloc_ipc(void) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.ipc_cache[i].used) {
            ipc_ns_t *ipc = &ns_data.ipc_cache[i];
            memset(ipc, 0, sizeof(*ipc));
            ipc->used = 1;
            ipc->refcount = 1;
            ipc->sem_ids = 0;
            ipc->msg_ids = 0;
            ipc->shm_ids = 0;
            ns_data.ipc_count++;
            return ipc;
        }
    }
    return NULL;
}

static user_ns_t *ns_alloc_user(user_ns_t *parent, uint32_t owner) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.user_cache[i].used) {
            user_ns_t *uns = &ns_data.user_cache[i];
            memset(uns, 0, sizeof(*uns));
            uns->used = 1;
            uns->refcount = 1;
            uns->parent = parent;
            uns->level = parent ? parent->level + 1 : 0;
            uns->owner = owner;
            ns_data.user_count++;
            return uns;
        }
    }
    return NULL;
}

static nsproxy_t *ns_alloc_proxy(void) {
    for (uint32_t i = 0; i < NAMESPACE_MAX_PROXIES; i++) {
        if (!ns_data.proxies[i].used) {
            nsproxy_t *np = &ns_data.proxies[i];
            memset(np, 0, sizeof(*np));
            np->used = 1;
            np->refcount = 1;
            ns_data.proxy_count++;
            return np;
        }
    }
    return NULL;
}

uts_ns_t *uts_ns_create(void) {
    return ns_alloc_uts();
}

pid_ns_t *pid_ns_create(pid_ns_t *parent) {
    return ns_alloc_pid(parent);
}

mnt_ns_t *mnt_ns_create(void) {
    return ns_alloc_mnt();
}

ipc_ns_t *ipc_ns_create(void) {
    return ns_alloc_ipc();
}

user_ns_t *user_ns_create(user_ns_t *parent, uint32_t owner) {
    return ns_alloc_user(parent, owner);
}

int namespace_init(void) {
    if (ns_data.initialized) return 0;
    memset(&ns_data, 0, sizeof(ns_data));

    nsproxy_t *init_np = ns_alloc_proxy();
    if (!init_np) return -28;

    init_np->uts_ns = ns_alloc_uts();
    init_np->pid_ns = ns_alloc_pid(NULL);
    init_np->mnt_ns = ns_alloc_mnt();
    init_np->ipc_ns = ns_alloc_ipc();
    init_np->user_ns = ns_alloc_user(NULL, 0);
    init_np->net_ns = netns_get();

    ns_data.init_proxy = init_np;
    ns_data.initialized = 1;

    klog_info("namespace: namespaces subsystem initialized (proxies=%u, uts=%u, pid=%u, mnt=%u, ipc=%u, user=%u)",
              ns_data.proxy_count, ns_data.uts_count, ns_data.pid_count,
              ns_data.mnt_count, ns_data.ipc_count, ns_data.user_count);
    return 0;
}

nsproxy_t *nsproxy_get(void) {
    if (!ns_data.initialized) return NULL;
    ns_data.total_gets++;
    nsproxy_t *np = ns_data.init_proxy;
    if (np) np->refcount++;
    return np;
}

void get_nsproxy(nsproxy_t *ns) {
    if (!ns) return;
    ns->refcount++;
    ns_data.total_gets++;
}

void put_nsproxy(nsproxy_t *ns) {
    if (!ns) return;
    if (ns->refcount > 0) ns->refcount--;
    ns_data.total_puts++;
}

int copy_namespaces(unsigned long flags, nsproxy_t *old, nsproxy_t **newp) {
    if (!newp) return -22;
    if (!ns_data.initialized) return -19;

    *newp = NULL;
    nsproxy_t *new_np = ns_alloc_proxy();
    if (!new_np) return -28;

    if (!old) old = ns_data.init_proxy;

    new_np->uts_ns = (flags & CLONE_NEWUTS) ? ns_alloc_uts() : old->uts_ns;
    new_np->pid_ns = (flags & CLONE_NEWPID) ? ns_alloc_pid(old->pid_ns) : old->pid_ns;
    new_np->mnt_ns = (flags & CLONE_NEWNS) ? ns_alloc_mnt() : old->mnt_ns;
    new_np->ipc_ns = (flags & CLONE_NEWIPC) ? ns_alloc_ipc() : old->ipc_ns;
    new_np->user_ns = (flags & CLONE_NEWUSER) ? ns_alloc_user(old->user_ns, 0) : old->user_ns;
    new_np->net_ns = (flags & CLONE_NEWNET) ? netns_get() : old->net_ns;

    if (new_np->uts_ns && !(flags & CLONE_NEWUTS)) new_np->uts_ns->refcount++;
    if (new_np->pid_ns && !(flags & CLONE_NEWPID)) new_np->pid_ns->refcount++;
    if (new_np->mnt_ns && !(flags & CLONE_NEWNS)) new_np->mnt_ns->refcount++;
    if (new_np->ipc_ns && !(flags & CLONE_NEWIPC)) new_np->ipc_ns->refcount++;
    if (new_np->user_ns && !(flags & CLONE_NEWUSER)) new_np->user_ns->refcount++;
    if (!(flags & CLONE_NEWNET) && new_np->net_ns) {
        netns_put(new_np->net_ns);
    }

    ns_data.total_copies++;
    *newp = new_np;
    klog_info("namespace: copied namespaces (flags=0x%lx)", flags);
    return 0;
}

int create_new_namespace(unsigned long flags, nsproxy_t **newp) {
    return copy_namespaces(flags, NULL, newp);
}

void namespace_print_stats(void) {
    if (!ns_data.initialized) {
        klog_info("namespace: not initialized");
        return;
    }

    nsproxy_t *init = ns_data.init_proxy;

    klog_info("=== Namespace Subsystem Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total nsproxy allocations: %u/%d", ns_data.proxy_count, NAMESPACE_MAX_PROXIES);
    klog_info("UTS namespaces: %u/%d", ns_data.uts_count, NAMESPACE_MAX_PROXIES);
    klog_info("PID namespaces: %u/%d", ns_data.pid_count, NAMESPACE_MAX_PROXIES);
    klog_info("Mount namespaces: %u/%d", ns_data.mnt_count, NAMESPACE_MAX_PROXIES);
    klog_info("IPC namespaces: %u/%d", ns_data.ipc_count, NAMESPACE_MAX_PROXIES);
    klog_info("User namespaces: %u/%d", ns_data.user_count, NAMESPACE_MAX_PROXIES);
    klog_info("Total get_nsproxy() calls: %llu", (unsigned long long)ns_data.total_gets);
    klog_info("Total put_nsproxy() calls: %llu", (unsigned long long)ns_data.total_puts);
    klog_info("Total copies: %llu", (unsigned long long)ns_data.total_copies);
    klog_info("Total creates: %llu", (unsigned long long)ns_data.total_creates);
    klog_info("");

    klog_info("Init namespace proxy:");
    klog_info("  refcount: %u", init->refcount);
    if (init->uts_ns) {
        klog_info("  UTS: nodename='%s' domain='%s' release='%s'",
                  init->uts_ns->nodename, init->uts_ns->domainname,
                  init->uts_ns->release);
    }
    if (init->pid_ns) {
        klog_info("  PID: level=%u last_pid=%d allocated=%u",
                  init->pid_ns->level, init->pid_ns->last_pid,
                  init->pid_ns->pid_allocated);
    }
    if (init->mnt_ns) {
        klog_info("  Mount: mount_count=%u root_mnt=%u",
                  init->mnt_ns->mount_count, init->mnt_ns->root_mnt);
    }
    if (init->ipc_ns) {
        klog_info("  IPC: sem=%u msg=%u shm=%u",
                  init->ipc_ns->sem_ids, init->ipc_ns->msg_ids,
                  init->ipc_ns->shm_ids);
    }
    if (init->user_ns) {
        klog_info("  User: level=%u owner=%d",
                  init->user_ns->level, init->user_ns->owner);
    }
    if (init->net_ns) {
        klog_info("  Net: referencing netns subsystem");
    }
}
