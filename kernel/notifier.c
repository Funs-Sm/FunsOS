#include "notifier.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"

static notifier_block_t *g_chains[NOTIFIER_MAX_CHAINS];
static int g_initialized = 0;

void notifier_init(void) {
    for (int i = 0; i < NOTIFIER_MAX_CHAINS; i++) {
        g_chains[i] = NULL;
    }
    g_initialized = 1;
    klog_info("notifier: subsystem initialized (%d chains)", NOTIFIER_MAX_CHAINS);
}

int notifier_register(uint32_t chain, notifier_fn_t callback,
                      uint32_t priority, void *user_data) {
    if (!g_initialized) notifier_init();
    if (chain >= NOTIFIER_MAX_CHAINS || !callback) return -22;

    notifier_block_t *nb = kmalloc(sizeof(notifier_block_t));
    if (!nb) return -12;

    nb->callback = callback;
    nb->priority = priority;
    nb->user_data = user_data;
    nb->next = NULL;

    /* 按优先级插入（从小到大，先调用高优先级） */
    notifier_block_t **pp = &g_chains[chain];
    while (*pp && (*pp)->priority <= priority) {
        pp = &(*pp)->next;
    }
    nb->next = *pp;
    *pp = nb;

    return 0;
}

int notifier_unregister(uint32_t chain, notifier_fn_t callback) {
    if (chain >= NOTIFIER_MAX_CHAINS || !callback) return -22;

    notifier_block_t **pp = &g_chains[chain];
    while (*pp) {
        if ((*pp)->callback == callback) {
            notifier_block_t *nb = *pp;
            *pp = nb->next;
            kfree(nb);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -2;
}

int notifier_call_chain(uint32_t chain, uint32_t event, void *data) {
    if (chain >= NOTIFIER_MAX_CHAINS) return -22;

    int ret = 0;
    notifier_block_t *nb = g_chains[chain];
    while (nb) {
        ret = nb->callback(data, event);
        if (ret != 0) break;
        nb = nb->next;
    }
    return ret;
}

uint32_t notifier_count(uint32_t chain) {
    if (chain >= NOTIFIER_MAX_CHAINS) return 0;

    uint32_t count = 0;
    notifier_block_t *nb = g_chains[chain];
    while (nb) {
        count++;
        nb = nb->next;
    }
    return count;
}
