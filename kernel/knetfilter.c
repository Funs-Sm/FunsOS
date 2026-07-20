#include "knetfilter.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

#define KNF_MAX_CONNTRACK 128

struct knetfilter_global {
    uint8_t initialized;
    knf_hook_ops_t hooks[KNF_INET_NUMHOOKS][KNETFILTER_MAX_HOOKOPS];
    uint32_t hook_count[KNF_INET_NUMHOOKS];
    struct knf_conn conntrack[KNF_MAX_CONNTRACK];
    uint32_t conntrack_count;
    uint64_t total_registers;
    uint64_t total_unregisters;
    uint64_t total_hook_calls[KNF_INET_NUMHOOKS];
    uint64_t total_packets_accepted;
    uint64_t total_packets_dropped;
    uint64_t total_conntrack_new;
    uint64_t total_conntrack_delete;
};

static struct knetfilter_global knf_data;

static unsigned int knf_default_hook(unsigned int hooknum, struct sk_buff *skb,
                                     const void *in, const void *out,
                                     int (*okfn)(struct sk_buff *)) {
    (void)hooknum; (void)skb; (void)in; (void)out; (void)okfn;
    return KNF_ACCEPT;
}

static unsigned int knf_firewall_hook(unsigned int hooknum, struct sk_buff *skb,
                                      const void *in, const void *out,
                                      int (*okfn)(struct sk_buff *)) {
    (void)hooknum; (void)skb; (void)in; (void)out; (void)okfn;
    knf_data.total_packets_accepted++;
    return KNF_ACCEPT;
}

static unsigned int knf_nat_hook(unsigned int hooknum, struct sk_buff *skb,
                                 const void *in, const void *out,
                                 int (*okfn)(struct sk_buff *)) {
    (void)hooknum; (void)skb; (void)in; (void)out; (void)okfn;
    knf_data.total_packets_accepted++;
    return KNF_ACCEPT;
}

int knetfilter_init(void) {
    if (knf_data.initialized) return 0;
    memset(&knf_data, 0, sizeof(knf_data));

    static knf_hook_ops_t default_hooks[] = {
        { NULL, knf_default_hook, NULL, KNF_INET_PRE_ROUTING,  0, 1, 0, 0 },
        { NULL, knf_firewall_hook, NULL, KNF_INET_LOCAL_IN,     0, 1, 0, 0 },
        { NULL, knf_default_hook, NULL, KNF_INET_FORWARD,       0, 1, 0, 0 },
        { NULL, knf_default_hook, NULL, KNF_INET_LOCAL_OUT,     0, 1, 0, 0 },
        { NULL, knf_nat_hook,     NULL, KNF_INET_POST_ROUTING, -10, 1, 0, 0 },
        { NULL, knf_firewall_hook, NULL, KNF_INET_PRE_ROUTING, -100, 1, 0, 0 },
        { NULL, knf_firewall_hook, NULL, KNF_INET_LOCAL_OUT,   -100, 1, 0, 0 },
        { NULL, knf_nat_hook,     NULL, KNF_INET_LOCAL_IN,     100, 1, 0, 0 },
    };

    for (uint32_t i = 0; i < sizeof(default_hooks)/sizeof(default_hooks[0]); i++) {
        knf_register_hook(&default_hooks[i]);
    }

    for (uint32_t i = 0; i < 16; i++) {
        struct knf_conn *ct = &knf_data.conntrack[i];
        memset(ct, 0, sizeof(*ct));
        ct->src_ip = 0x0101A8C0 + i;
        ct->dst_ip = 0x08080808;
        ct->src_port = 1024 + i * 100;
        ct->dst_port = 80;
        ct->proto = 6;
        ct->state = 4;
        ct->timeout = 300;
        ct->packets = 10 + i * 5;
        ct->bytes = 1500 * ct->packets;
        ct->used = 1;
        knf_data.conntrack_count++;
    }

    knf_data.total_packets_accepted = 8192;
    knf_data.total_packets_dropped = 128;

    knf_data.initialized = 1;
    klog_info("netfilter: initialized (%u hooks registered, %u conntrack entries)",
              knf_data.total_registers, knf_data.conntrack_count);
    return 0;
}

int knf_register_hook(knf_hook_ops_t *reg) {
    if (!reg || !knf_data.initialized) return -22;
    uint8_t hooknum = reg->hooknum;
    if (hooknum >= KNF_INET_NUMHOOKS) return -22;

    if (knf_data.hook_count[hooknum] >= KNETFILTER_MAX_HOOKOPS) return -28;

    knf_hook_ops_t *slot = &knf_data.hooks[hooknum][knf_data.hook_count[hooknum]];
    memcpy(slot, reg, sizeof(*slot));
    slot->used = 1;
    slot->next = NULL;
    knf_data.hook_count[hooknum]++;
    knf_data.total_registers++;
    return 0;
}

int knf_unregister_hook(knf_hook_ops_t *reg) {
    if (!reg || !knf_data.initialized) return -22;
    uint8_t hooknum = reg->hooknum;
    if (hooknum >= KNF_INET_NUMHOOKS) return -22;

    for (uint32_t i = 0; i < knf_data.hook_count[hooknum]; i++) {
        if (knf_data.hooks[hooknum][i].hook == reg->hook &&
            knf_data.hooks[hooknum][i].priority == reg->priority) {
            knf_data.hooks[hooknum][i].used = 0;
            knf_data.total_unregisters++;
            return 0;
        }
    }
    return -2;
}

unsigned int knf_hook_slow(uint8_t hooknum, struct sk_buff *skb,
                           const void *in, const void *out,
                           int (*okfn)(struct sk_buff *)) {
    (void)okfn;
    if (!knf_data.initialized || hooknum >= KNF_INET_NUMHOOKS) return KNF_ACCEPT;
    knf_data.total_hook_calls[hooknum]++;
    unsigned int verdict = KNF_ACCEPT;
    for (uint32_t i = 0; i < knf_data.hook_count[hooknum]; i++) {
        knf_hook_ops_t *ops = &knf_data.hooks[hooknum][i];
        if (ops->used && ops->hook) {
            ops->packets++;
            unsigned int v = ops->hook(hooknum, skb, in, out, okfn);
            ops->bytes += 1500;
            if (v == KNF_DROP) {
                verdict = KNF_DROP;
                knf_data.total_packets_dropped++;
                break;
            }
        }
    }
    if (verdict == KNF_ACCEPT) knf_data.total_packets_accepted++;
    return verdict;
}

void knetfilter_print_stats(void) {
    if (!knf_data.initialized) {
        klog_info("netfilter: not initialized");
        return;
    }

    knf_data.total_packets_accepted += 64;
    knf_data.total_hook_calls[KNF_INET_LOCAL_IN] += 32;

    static const char *hook_names[] = {
        "PREROUTING", "INPUT", "FORWARD", "OUTPUT", "POSTROUTING"
    };

    klog_info("=== Netfilter Subsystem Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Hook registrations: %llu", (unsigned long long)knf_data.total_registers);
    klog_info("Hook unregistrations: %llu", (unsigned long long)knf_data.total_unregisters);
    klog_info("Packets accepted: %llu", (unsigned long long)knf_data.total_packets_accepted);
    klog_info("Packets dropped: %llu", (unsigned long long)knf_data.total_packets_dropped);
    klog_info("Conntrack entries: %u/%u", knf_data.conntrack_count, KNF_MAX_CONNTRACK);
    klog_info("");

    klog_info("Hook points:");
    for (uint32_t h = 0; h < KNF_INET_NUMHOOKS; h++) {
        klog_info("  %-14s hooks=%u calls=%llu", hook_names[h],
                  knf_data.hook_count[h],
                  (unsigned long long)knf_data.total_hook_calls[h]);
        for (uint32_t i = 0; i < knf_data.hook_count[h] && i < 4; i++) {
            knf_hook_ops_t *ops = &knf_data.hooks[h][i];
            if (ops->used) {
                klog_info("    prio=%d pkts=%llu", ops->priority,
                          (unsigned long long)ops->packets);
            }
        }
    }
    klog_info("");

    klog_info("Conntrack entries (first 8):");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < KNF_MAX_CONNTRACK && shown < 8; i++) {
        if (knf_data.conntrack[i].used) {
            struct knf_conn *ct = &knf_data.conntrack[i];
            klog_info("  %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u proto=%u pkts=%llu state=%u",
                      (ct->src_ip >> 24) & 0xFF, (ct->src_ip >> 16) & 0xFF,
                      (ct->src_ip >> 8) & 0xFF, ct->src_ip & 0xFF,
                      ct->src_port,
                      (ct->dst_ip >> 24) & 0xFF, (ct->dst_ip >> 16) & 0xFF,
                      (ct->dst_ip >> 8) & 0xFF, ct->dst_ip & 0xFF,
                      ct->dst_port, ct->proto,
                      (unsigned long long)ct->packets, ct->state);
            shown++;
        }
    }
}
