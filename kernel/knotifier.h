#ifndef KNOTIFIER_H
#define KNOTIFIER_H

#include "stdint.h"

#define KNOTIFY_DONE        0x0000
#define KNOTIFY_OK          0x0001
#define KNOTIFY_STOP_MASK   0x8000
#define KNOTIFY_BAD         (KNOTIFY_STOP_MASK|0x0002)
#define KNOTIFY_STOP        (KNOTIFY_OK|KNOTIFY_STOP_MASK)

#define KNOTIFIER_PRIORITY_HIGHEST  0x7FFFFFFF
#define KNOTIFIER_PRIORITY_HIGH     1000
#define KNOTIFIER_PRIORITY_DEFAULT  0
#define KNOTIFIER_PRIORITY_LOW      -1000
#define KNOTIFIER_PRIORITY_LOWEST   (-0x7FFFFFFF-1)

struct knotifier_block;
typedef int (*knotifier_fn_t)(struct knotifier_block *nb, unsigned long action, void *data);

struct knotifier_block {
    struct knotifier_block *next;
    knotifier_fn_t knotifier_call;
    int priority;
    void *private_data;
};

struct atomic_knotifier_head {
    struct knotifier_block *head;
    const char *name;
    uint32_t call_count;
};

struct blocking_knotifier_head {
    struct knotifier_block *head;
    const char *name;
    uint32_t call_count;
};

int knotifier_chain_init(void);

int atomic_knotifier_chain_register(struct atomic_knotifier_head *nh, struct knotifier_block *nb);
int atomic_knotifier_chain_unregister(struct atomic_knotifier_head *nh, struct knotifier_block *nb);
int atomic_knotifier_call_chain(struct atomic_knotifier_head *nh, unsigned long val, void *v);

int blocking_knotifier_chain_register(struct blocking_knotifier_head *nh, struct knotifier_block *nb);
int blocking_knotifier_chain_unregister(struct blocking_knotifier_head *nh, struct knotifier_block *nb);
int blocking_knotifier_call_chain(struct blocking_knotifier_head *nh, unsigned long val, void *v);

#define ATOMIC_KNOTIFIER_INIT(name) { NULL, #name, 0 }
#define BLOCKING_KNOTIFIER_INIT(name) { NULL, #name, 0 }

#define ATOMIC_KNOTIFIER_HEAD(name) \
    struct atomic_knotifier_head name = ATOMIC_KNOTIFIER_INIT(name)
#define BLOCKING_KNOTIFIER_HEAD(name) \
    struct blocking_knotifier_head name = BLOCKING_KNOTIFIER_INIT(name)

extern struct atomic_knotifier_head panic_knotifier_list;
extern struct atomic_knotifier_head die_knotifier_list;
extern struct blocking_knotifier_head reboot_knotifier_list;
extern struct blocking_knotifier_head netdev_kchain;
extern struct blocking_knotifier_head cpu_kchain;

void knotifier_print_stats(void);

#endif
