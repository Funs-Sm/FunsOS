#include "knotifier.h"
#include "klog.h"
#include "string.h"
#include "sync.h"
#include "stdio.h"

struct atomic_knotifier_head panic_knotifier_list = ATOMIC_KNOTIFIER_INIT(panic);
struct atomic_knotifier_head die_knotifier_list = ATOMIC_KNOTIFIER_INIT(die);
struct blocking_knotifier_head reboot_knotifier_list = BLOCKING_KNOTIFIER_INIT(reboot);
struct blocking_knotifier_head netdev_kchain = BLOCKING_KNOTIFIER_INIT(netdev);
struct blocking_knotifier_head cpu_kchain = BLOCKING_KNOTIFIER_INIT(cpu);

static uint8_t knotifier_initialized = 0;
static spinlock_t knotifier_lock;

static int knotifier_chain_register_internal(struct knotifier_block **nl, struct knotifier_block *nb) {
    struct knotifier_block *prev = NULL;
    struct knotifier_block *curr = *nl;

    while (curr && curr->priority > nb->priority) {
        prev = curr;
        curr = curr->next;
    }

    nb->next = curr;
    if (prev) {
        prev->next = nb;
    } else {
        *nl = nb;
    }
    return 0;
}

static int knotifier_chain_unregister_internal(struct knotifier_block **nl, struct knotifier_block *nb) {
    struct knotifier_block *prev = NULL;
    struct knotifier_block *curr = *nl;

    while (curr) {
        if (curr == nb) {
            if (prev) {
                prev->next = curr->next;
            } else {
                *nl = curr->next;
            }
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

static int knotifier_call_chain_internal(struct knotifier_block **nl, unsigned long val, void *v, uint32_t *count) {
    int ret = KNOTIFY_DONE;
    struct knotifier_block *nb = *nl;

    while (nb) {
        if (nb->knotifier_call) {
            ret = nb->knotifier_call(nb, val, v);
            if (count) (*count)++;
            if (ret & KNOTIFY_STOP_MASK) break;
        }
        nb = nb->next;
    }
    return ret;
}

int knotifier_chain_init(void) {
    if (knotifier_initialized) return 0;
    spinlock_init(&knotifier_lock);
    knotifier_initialized = 1;
    klog_info("Notifier chain subsystem initialized");
    return 0;
}

int atomic_knotifier_chain_register(struct atomic_knotifier_head *nh, struct knotifier_block *nb) {
    if (!nh || !nb || !nb->knotifier_call) return -1;
    spinlock_lock(&knotifier_lock);
    int ret = knotifier_chain_register_internal(&nh->head, nb);
    spinlock_unlock(&knotifier_lock);
    return ret;
}

int atomic_knotifier_chain_unregister(struct atomic_knotifier_head *nh, struct knotifier_block *nb) {
    if (!nh || !nb) return -1;
    spinlock_lock(&knotifier_lock);
    int ret = knotifier_chain_unregister_internal(&nh->head, nb);
    spinlock_unlock(&knotifier_lock);
    return ret;
}

int atomic_knotifier_call_chain(struct atomic_knotifier_head *nh, unsigned long val, void *v) {
    if (!nh) return KNOTIFY_DONE;
    nh->call_count++;
    return knotifier_call_chain_internal(&nh->head, val, v, NULL);
}

int blocking_knotifier_chain_register(struct blocking_knotifier_head *nh, struct knotifier_block *nb) {
    if (!nh || !nb || !nb->knotifier_call) return -1;
    spinlock_lock(&knotifier_lock);
    int ret = knotifier_chain_register_internal(&nh->head, nb);
    spinlock_unlock(&knotifier_lock);
    return ret;
}

int blocking_knotifier_chain_unregister(struct blocking_knotifier_head *nh, struct knotifier_block *nb) {
    if (!nh || !nb) return -1;
    spinlock_lock(&knotifier_lock);
    int ret = knotifier_chain_unregister_internal(&nh->head, nb);
    spinlock_unlock(&knotifier_lock);
    return ret;
}

int blocking_knotifier_call_chain(struct blocking_knotifier_head *nh, unsigned long val, void *v) {
    if (!nh) return KNOTIFY_DONE;
    nh->call_count++;
    return knotifier_call_chain_internal(&nh->head, val, v, NULL);
}

static uint32_t count_knotifiers(struct knotifier_block *nb) {
    uint32_t cnt = 0;
    while (nb) { cnt++; nb = nb->next; }
    return cnt;
}

void knotifier_print_stats(void) {
    klog_info("=== Notifier Chains ===");
    klog_info("  panic_notifier:    %u blocks, called %u times",
             count_knotifiers(panic_knotifier_list.head), panic_knotifier_list.call_count);
    klog_info("  die_notifier:      %u blocks, called %u times",
             count_knotifiers(die_knotifier_list.head), die_knotifier_list.call_count);
    klog_info("  reboot_notifier:   %u blocks, called %u times",
             count_knotifiers(reboot_knotifier_list.head), reboot_knotifier_list.call_count);
    klog_info("  netdev_chain:      %u blocks, called %u times",
             count_knotifiers(netdev_kchain.head), netdev_kchain.call_count);
    klog_info("  cpu_chain:         %u blocks, called %u times",
             count_knotifiers(cpu_kchain.head), cpu_kchain.call_count);
}
