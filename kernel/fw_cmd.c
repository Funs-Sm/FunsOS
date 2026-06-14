#include "shell.h"
#include "fw.h"
#include "fw_bandwidth.h"
#include "net.h"
#include "netfilter.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdarg.h"
#include "stddef.h"
#include "stdint.h"

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* Local printf-style helper: format into a stack buffer, then drop
 * the result via the public shell_print. */
static void fw_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void fw_print(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    shell_print(buf);
}

static int parse_ip(const char *s, ipv4_addr_t *out) {
    if (!s || !out) return -1;
    uint8_t b[4] = {0, 0, 0, 0};
    int n = 0;
    int v = 0;
    int any = 0;
    for (const char *p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = 1; continue; }
        if (c == '.' || c == '\0') {
            if (!any || n >= 4 || v < 0 || v > 255) return -1;
            b[n++] = (uint8_t)v;
            v = 0; any = 0;
            if (c == '\0') break;
            continue;
        }
        return -1;
    }
    if (n != 4) return -1;
    out->addr = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) |
                ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static ipv4_addr_t ip_from_str_or_zero(const char *s) {
    ipv4_addr_t a = {0};
    parse_ip(s, &a);
    return a;
}

static ipv4_addr_t mask_from_prefix(int pfx) {
    ipv4_addr_t m = {0};
    if (pfx <= 0) return m;
    if (pfx > 32) pfx = 32;
    m.addr = (pfx == 32) ? 0xFFFFFFFFu : (~0u << (32 - pfx));
    return m;
}

static int parse_proto(const char *s) {
    if (!s) return 0;
    if (strcmp(s, "tcp")  == 0) return 6;
    if (strcmp(s, "udp")  == 0) return 17;
    if (strcmp(s, "icmp") == 0) return 1;
    return 0;
}

static const char *proto_name(uint8_t p) {
    switch (p) {
        case 6:  return "tcp";
        case 17: return "udp";
        case 1:  return "icmp";
        case 0:  return "any";
        default: return "?";
    }
}

static const char *flow_state_name(uint8_t s) {
    switch (s) {
        case FW_FLOW_NEW:         return "NEW";
        case FW_FLOW_ESTABLISHED: return "ESTABLISHED";
        case FW_FLOW_RELATED:     return "RELATED";
        default:                  return "INVALID";
    }
}

/* ------------------------------------------------------------------ */
/* fw status / enable / disable                                       */
/* ------------------------------------------------------------------ */

void cmd_fw_status(void) {
    const fw_stats_t *s = fw_get_stats();
    fw_print("Firewall: %s\n", fw_is_enabled() ? "enabled" : "disabled");
    fw_print("  accepted:  %u\n", s->packets_accepted);
    fw_print("  dropped:   %u\n", s->packets_dropped);
    fw_print("  nat'd:     %u\n", s->packets_nat);
    fw_print("  invalid:   %u\n", s->packets_invalid);
    fw_print("  conntracks:%u\n", s->conntracks_active);
    fw_print("  nat rules: %u\n", s->nat_rules_active);
}

void cmd_fw_enable(const char *onoff) {
    if (!onoff) { fw_print("fw: missing on/off\n"); return; }
    if (strcmp(onoff, "on") == 0) {
        fw_set_enabled(1);
        fw_print("Firewall enabled\n");
    } else if (strcmp(onoff, "off") == 0) {
        fw_set_enabled(0);
        fw_print("Firewall disabled\n");
    } else {
        fw_print("fw: use 'on' or 'off'\n");
    }
}

/* ------------------------------------------------------------------ */
/* conntrack                                                           */
/* ------------------------------------------------------------------ */

void cmd_fw_conntrack(const char *subcmd) {
    if (!subcmd) subcmd = "list";
    if (strcmp(subcmd, "list") == 0) {
        fw_print("  %-3s %-5s %-22s %-22s %-12s %-10s %-7s %-7s %s\n",
                    "PRO", "STATE", "SRC", "DST", "SP:DP", "PACKETS",
                    "BYTES", "AGE_ms", "NAT");
        uint32_t now = fw_get_stats()->conntracks_active;  /* not used */
        for (uint32_t i = 0; i < FW_CONN_MAX; i++) {
            const fw_conn_t *c = fw_conntrack_at(i);
            if (!c) continue;
            char src[20], dst[20], nat[16] = "-";
            fw_ip_to_str(c->src_ip, src, sizeof(src));
            fw_ip_to_str(c->dst_ip, dst, sizeof(dst));
            if (c->nat_type == 1) strncpy(nat, "SNAT",  sizeof(nat) - 1);
            else if (c->nat_type == 2) strncpy(nat, "DNAT",  sizeof(nat) - 1);
            else if (c->nat_type == 3) strncpy(nat, "MASQ",  sizeof(nat) - 1);
            (void)now;
            fw_print("  %-3s %-5s %-22s %-22s %u:%-7u %-10u %-7u %s\n",
                        proto_name(c->proto),
                        flow_state_name(c->state),
                        src, dst,
                        c->src_port, c->dst_port,
                        c->packets, c->bytes, nat);
        }
    } else if (strcmp(subcmd, "flush") == 0) {
        fw_conntrack_flush();
        fw_print("Conntrack table flushed\n");
    } else if (strcmp(subcmd, "count") == 0) {
        fw_print("%u conntrack entries\n", fw_conntrack_count());
    } else {
        fw_print("fw conntrack: list|flush|count\n");
    }
}

/* ------------------------------------------------------------------ */
/* NAT rules                                                           */
/* ------------------------------------------------------------------ */

void cmd_fw_nat_list(void) {
    fw_print("  %-3s %-5s %-18s %-18s %-10s %-15s %-7s\n",
                "IDX", "TYPE", "ORIG-SRC", "ORIG-DST", "PROTO", "TRANS", "HITS");
    for (uint32_t i = 0; i < FW_NAT_RULES_MAX; i++) {
        const fw_nat_rule_t *r = fw_nat_at(i);
        if (!r) continue;
        char os[20], od[20], ts[20] = "-";
        fw_ip_to_str(r->orig_src, os, sizeof(os));
        fw_ip_to_str(r->orig_dst, od, sizeof(od));
        if (r->type == FW_NAT_SNAT || r->type == FW_NAT_MASQUERADE) {
            fw_ip_to_str(r->trans_src, ts, sizeof(ts));
        } else if (r->type == FW_NAT_DNAT) {
            fw_ip_to_str(r->trans_dst, ts, sizeof(ts));
        }
        const char *tn = (r->type == FW_NAT_SNAT) ? "SNAT" :
                         (r->type == FW_NAT_DNAT) ? "DNAT" : "MASQ";
        fw_print("  %-3u %-5s %-18s %-18s %-10s %-15s %u\n",
                    i, tn, os, od, proto_name(r->proto), ts, r->counter);
    }
}

void cmd_fw_nat_add(const char **argv, int argc) {
    /* fw nat add <snat|dnat|masq> [proto] <orig-src> <orig-dst> <trans>
     * argv[0]="add", argv[1]=type, argv[2]=proto, ...
     */
    if (argc < 2) {
        fw_print("usage: fw nat add <snat|dnat|masq> [tcp|udp|icmp] "
                    "<orig-src> <orig-dst> <trans>\n");
        return;
    }
    const char *type = argv[1];
    uint8_t proto = 0;
    int idx = 2;
    if (argc >= 3 && parse_proto(argv[2]) != 0) {
        proto = (uint8_t)parse_proto(argv[2]);
        idx = 3;
    }
    if (argc < idx + 3) {
        fw_print("fw nat: need orig-src, orig-dst, and trans address\n");
        return;
    }

    fw_nat_rule_t r;
    memset(&r, 0, sizeof(r));
    r.proto = proto;
    r.orig_src = ip_from_str_or_zero(argv[idx]);
    r.orig_dst = ip_from_str_or_zero(argv[idx + 1]);
    /* Default mask = /32 so single addresses match exactly. */
    r.orig_src_mask = mask_from_prefix(32);
    r.orig_dst_mask = mask_from_prefix(32);

    ipv4_addr_t trans = ip_from_str_or_zero(argv[idx + 2]);

    if (strcmp(type, "snat") == 0) {
        r.type = FW_NAT_SNAT;
        r.trans_src = trans;
    } else if (strcmp(type, "dnat") == 0) {
        r.type = FW_NAT_DNAT;
        r.trans_dst = trans;
    } else if (strcmp(type, "masq") == 0 || strcmp(type, "masquerade") == 0) {
        r.type = FW_NAT_MASQUERADE;
        r.trans_src = trans;
    } else {
        fw_print("fw nat: unknown type '%s'\n", type);
        return;
    }

    int rc = fw_nat_add(&r);
    if (rc < 0) fw_print("fw nat: rule table full\n");
    else        fw_print("fw nat: added rule #%d\n", rc);
}

void cmd_fw_nat_delete(const char *arg) {
    if (!arg) { fw_print("usage: fw nat delete <idx>\n"); return; }
    int idx = 0;
    while (*arg >= '0' && *arg <= '9') { idx = idx * 10 + (*arg - '0'); arg++; }
    if (fw_nat_delete((uint32_t)idx) == 0)
        fw_print("fw nat: deleted rule #%d\n", idx);
    else
        fw_print("fw nat: no such rule #%d\n", idx);
}

void cmd_fw_nat_flush(void) {
    fw_nat_flush();
    fw_print("NAT rule table flushed\n");
}

void cmd_fw_nat(const char *subcmd, const char **argv, int argc) {
    if (!subcmd || strcmp(subcmd, "list") == 0) {
        cmd_fw_nat_list();
    } else if (strcmp(subcmd, "add") == 0) {
        cmd_fw_nat_add(argv, argc);
    } else if (strcmp(subcmd, "delete") == 0) {
        cmd_fw_nat_delete(argv ? argv[1] : NULL);
    } else if (strcmp(subcmd, "flush") == 0) {
        cmd_fw_nat_flush();
    } else {
        fw_print("fw nat: list|add|delete|flush\n");
    }
}

/* ------------------------------------------------------------------ */
/* Tables / chains                                                     */
/* ------------------------------------------------------------------ */

static const char *hook_name(int h) {
    switch (h) {
        case NF_INET_PRE_ROUTING:  return "PREROUTING";
        case NF_INET_LOCAL_IN:     return "INPUT";
        case NF_INET_FORWARD:      return "FORWARD";
        case NF_INET_LOCAL_OUT:    return "OUTPUT";
        case NF_INET_POST_ROUTING: return "POSTROUTING";
        default:                   return "?";
    }
}

void cmd_fw_tables(void) {
    uint32_t n = nf_table_count();
    fw_print("%u table(s):\n", n);
    for (uint32_t i = 0; i < n; i++) {
        const nf_table_t *t = nf_table_at(i);
        if (!t) continue;
        fw_print("  %-12s family=%u\n", t->name, t->family);
        for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
            const nf_chain_t *c = &t->chains[h];
            if (!c->active && c->rule_count == 0) continue;
            fw_print("    chain %-12s policy=%s rules=%u\n",
                        hook_name(h),
                        c->default_policy == NF_DROP ? "DROP" : "ACCEPT",
                        c->rule_count);
        }
    }
}

void cmd_fw_chain_list(const char *tname, int hook) {
    if (!tname) { fw_print("usage: fw chain list <table> [hook]\n"); return; }
    nf_table_t *t = nf_table_get(tname);
    if (!t) { fw_print("no such table '%s'\n", tname); return; }
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) {
        for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
            cmd_fw_chain_list(tname, h);
        }
        return;
    }
    nf_chain_t *c = &t->chains[hook];
    fw_print("Table %s chain %s (%s, policy=%s, %u rules):\n",
                t->name, hook_name(hook),
                c->active ? "active" : "inactive",
                c->default_policy == NF_DROP ? "DROP" : "ACCEPT",
                c->rule_count);
    for (uint32_t i = 0; i < c->rule_count; i++) {
        const nf_rule_t *r = &c->rules[i];
        char sip[20], dip[20];
        fw_ip_to_str(r->match.src_ip, sip, sizeof(sip));
        fw_ip_to_str(r->match.dst_ip, dip, sizeof(dip));
        fw_print("  #%-3u target=%s proto=%-4s %s:%u -> %s:%u hits=%u %s\n",
                    i,
                    r->target == NF_DROP ? "DROP" : "ACCEPT",
                    proto_name(r->match.protocol),
                    sip, r->match.src_port,
                    dip, r->match.dst_port,
                    r->counter,
                    r->comment[0] ? r->comment : "");
    }
}

void cmd_fw_policy(const char *tname, const char *hook_s, const char *pol_s) {
    if (!tname || !hook_s || !pol_s) {
        fw_print("usage: fw policy <table> <chain> <accept|drop>\n");
        return;
    }
    int h = -1;
    for (int i = 0; i < NF_INET_NUMHOOKS; i++) {
        if (strcasecmp(hook_name(i), hook_s) == 0) { h = i; break; }
    }
    if (h < 0) { fw_print("fw policy: unknown chain '%s'\n", hook_s); return; }
    uint32_t policy = (strcasecmp(pol_s, "drop") == 0) ? NF_DROP : NF_ACCEPT;
    if (nf_chain_set_policy(tname, h, policy) == 0)
        fw_print("fw policy: %s/%s = %s\n", tname, hook_name(h), pol_s);
    else
        fw_print("fw policy: no such table\n");
}

void cmd_fw_table_create(const char *name) {
    if (!name) { fw_print("usage: fw table create <name>\n"); return; }
    if (nf_table_create(name) == 0)
        fw_print("fw table: created '%s'\n", name);
    else
        fw_print("fw table: '%s' already exists or no space\n", name);
}

void cmd_fw_table_attach(const char *name, const char *hook_s) {
    if (!name || !hook_s) {
        fw_print("usage: fw table attach <name> <chain>\n");
        return;
    }
    int h = -1;
    for (int i = 0; i < NF_INET_NUMHOOKS; i++) {
        if (strcasecmp(hook_name(i), hook_s) == 0) { h = i; break; }
    }
    if (h < 0) { fw_print("fw table: unknown chain '%s'\n", hook_s); return; }
    if (nf_table_attach(name, h) == 0)
        fw_print("fw table: '%s' attached to %s\n", name, hook_name(h));
    else
        fw_print("fw table: attach failed\n");
}

/* ------------------------------------------------------------------ */
/* Bandwidth / qdisc                                                   */
/* ------------------------------------------------------------------ */

static const char *human_rate(uint32_t bps, char *out, uint32_t out_size) {
    if (!out || out_size < 8) return "";
    if (bps >= 1000000u) {
        /* 2 decimal places of Mbps */
        uint32_t whole = bps / 1000000u;
        uint32_t frac  = (bps % 1000000u) / 10000u;
        snprintf(out, out_size, "%u.%02u Mbps", whole, frac);
    } else if (bps >= 1000u) {
        snprintf(out, out_size, "%u Kbps", bps / 1000u);
    } else {
        snprintf(out, out_size, "%u bps", bps);
    }
    return out;
}

void cmd_fw_limit_list(void) {
    uint32_t n = fw_qdisc_count();
    fw_print("%u qdisc(s):\n", n);
    for (uint32_t i = 0; i < FW_QDISC_MAX; i++) {
        const fw_qdisc_t *q = fw_qdisc_at(i);
        if (!q) continue;
        char rate[24];
        human_rate(q->rate_bps, rate, sizeof(rate));
        fw_print("  %-6s rate=%-12s burst=%-7u pass=%u drop=%u "
                    "(%u bytes pass / %u bytes drop)\n",
                    q->name, rate, q->burst_bytes,
                    q->packets_pass, q->packets_drop,
                    q->bytes_pass, q->bytes_drop);
    }
}

static int parse_rate(const char *s, uint32_t *out_bps) {
    if (!s || !out_bps) return -1;
    /* Accept "1mbps", "1M", "1024kbps", "500k", "2000" (raw bps). */
    uint32_t v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (!any) return -1;
    uint32_t mult = 1;
    if (*s == 'k' || *s == 'K') { mult = 1000u; s++; }
    else if (*s == 'm' || *s == 'M') { mult = 1000000u; s++; }
    if (*s == 'b' || *s == 'B') s++;
    if (*s == 'p' || *s == 'P') {
        if (s[1] == 's' || s[1] == 'S') s++;
    }
    /* Ignore trailing 'ps' or junk; we already consumed. */
    *out_bps = v * mult;
    return 0;
}

void cmd_fw_limit_add(const char *iface, const char *rate_s, const char *burst_s) {
    if (!iface || !rate_s) {
        fw_print("usage: fw limit add <iface> <rate>[k|m]bps [burst_bytes]\n");
        return;
    }
    uint32_t bps = 0;
    if (parse_rate(rate_s, &bps) != 0 || bps == 0) {
        fw_print("fw limit: bad rate '%s'\n", rate_s);
        return;
    }
    uint32_t burst = burst_s ? (uint32_t)atoi(burst_s) : (bps / 8u / 10u);
    if (burst == 0) burst = 1500;
    int rc = fw_qdisc_add(iface, bps, burst);
    if (rc < 0) fw_print("fw limit: add failed\n");
    else        fw_print("fw limit: %s rate=%u bps burst=%u\n", iface, bps, burst);
}

void cmd_fw_limit_del(const char *iface) {
    if (!iface) { fw_print("usage: fw limit del <iface>\n"); return; }
    if (fw_qdisc_delete(iface) == 0) fw_print("fw limit: removed '%s'\n", iface);
    else                              fw_print("fw limit: no such rule '%s'\n", iface);
}

void cmd_fw_limit_flush(void) {
    fw_qdisc_flush();
    fw_print("All qdiscs removed\n");
}

void cmd_fw_limit(const char *subcmd, const char **argv, int argc) {
    if (!subcmd || strcmp(subcmd, "list") == 0) {
        cmd_fw_limit_list();
    } else if (strcmp(subcmd, "add") == 0) {
        cmd_fw_limit_add(argv[1], argv[2], argv[3]);
    } else if (strcmp(subcmd, "del") == 0 || strcmp(subcmd, "delete") == 0) {
        cmd_fw_limit_del(argv[1]);
    } else if (strcmp(subcmd, "flush") == 0) {
        cmd_fw_limit_flush();
    } else {
        fw_print("fw limit: list|add|del|flush\n");
    }
}

/* ------------------------------------------------------------------ */
/* Top-level fw dispatcher                                             */
/* ------------------------------------------------------------------ */

void cmd_fw(const char *line) {
    /* Skip "fw" */
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != ' ' && *line != '\t') line++;
    while (*line == ' ' || *line == '\t') line++;

    /* Tokenize the rest. */
    char buf[256];
    int n = 0;
    while (line[n] && n < 255) { buf[n] = line[n]; n++; }
    buf[n] = '\0';

    const char *argv[16] = {0};
    int argc = 0;
    char *p = buf;
    while (*p && argc < 16) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }

    if (argc == 0) {
        fw_print("Firewall commands:\n");
        fw_print("  fw status                    -- show counters / state\n");
        fw_print("  fw enable | disable          -- master switch\n");
        fw_print("  fw tables                    -- list rule tables\n");
        fw_print("  fw table create <name>       -- create rule table\n");
        fw_print("  fw table attach <n> <chain>  -- attach table to hook\n");
        fw_print("  fw chain list <tbl> [chain]  -- list rules in a chain\n");
        fw_print("  fw policy <tbl> <chain> <a|d>-- set default chain policy\n");
        fw_print("  fw conntrack [list|flush]    -- connection tracking\n");
        fw_print("  fw nat list|add|delete|flush -- NAT rules\n");
        fw_print("  fw limit list|add|del|flush  -- bandwidth limits\n");
        return;
    }

    const char *cmd = argv[0];
    if (strcmp(cmd, "status") == 0)        cmd_fw_status();
    else if (strcmp(cmd, "enable") == 0)  cmd_fw_enable("on");
    else if (strcmp(cmd, "disable") == 0) cmd_fw_enable("off");
    else if (strcmp(cmd, "tables") == 0)   cmd_fw_tables();
    else if (strcmp(cmd, "table") == 0) {
        if (argc >= 2 && strcmp(argv[1], "create") == 0) cmd_fw_table_create(argv[2]);
        else if (argc >= 3 && strcmp(argv[1], "attach") == 0) cmd_fw_table_attach(argv[2], argv[3]);
        else fw_print("fw table: create <name> | attach <name> <chain>\n");
    }
    else if (strcmp(cmd, "chain") == 0) {
        if (argc >= 3 && strcmp(argv[1], "list") == 0) {
            int h = -1;
            for (int i = 0; i < NF_INET_NUMHOOKS; i++) {
                if (argv[3] && strcasecmp(hook_name(i), argv[3]) == 0) { h = i; break; }
            }
            cmd_fw_chain_list(argv[2], h);
        } else {
            fw_print("fw chain: list <table> [chain]\n");
        }
    }
    else if (strcmp(cmd, "policy") == 0) {
        cmd_fw_policy(argv[1], argv[2], argv[3]);
    }
    else if (strcmp(cmd, "conntrack") == 0) {
        cmd_fw_conntrack(argv[1]);
    }
    else if (strcmp(cmd, "nat") == 0) {
        cmd_fw_nat(argv[1], argv, argc);
    }
    else if (strcmp(cmd, "limit") == 0) {
        cmd_fw_limit(argv[1], argv, argc);
    }
    else {
        fw_print("fw: unknown subcommand '%s'\n", cmd);
    }
}
