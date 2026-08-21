#!/usr/bin/env python3
"""Fix shell.c calls to commands that take no args"""
with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# These take NO args - fix calls with () to remove
fixes = [
    'cmd_ping()', 'cmd_ipcs()', 'cmd_slabtop()', 'cmd_sync()',
    'cmd_pwd()', 'cmd_df()', 'cmd_netmon()', 'cmd_sigstat()',
    'cmd_mem()',  # if declared as void(void)
    'cmd_evlog()', 'cmd_quota()', 'cmd_quota_ext()', 'cmd_fim()',
    'cmd_snapshot()', 'cmd_mount2()', 'cmd_dcache()', 'cmd_icache()',
    'cmd_pagecache()', 'cmd_readahead_stat()', 'cmd_syncstat()',
    'cmd_kwork()', 'cmd_ktrace()', 'cmd_kprobe()', 'cmd_dumpstack()',
    'cmd_strace()', 'cmd_lsof()', 'cmd_prlimit()', 'cmd_capsh()',
    'cmd_sysreport()', 'cmd_notifier()', 'cmd_fsstat()', 'cmd_flock()',
    'cmd_flock_cmd()', 'cmd_dd_full()', 'cmd_ipc()', 'cmd_sysctl()',
    'cmd_nslookup()', 'cmd_dig()', 'cmd_dhcp()', 'cmd_tcpdump()',
    'cmd_traceroute()', 'cmd_nmap()', 'cmd_mtr()', 'cmd_dns()',
    'cmd_telnet()', 'cmd_fw()', 'cmd_ss()', 'cmd_tftp()', 'cmd_ntp()',
    'cmd_futexinfo()', 'cmd_epollinfo()', 'cmd_inotifyinfo()',
    'cmd_softirq()', 'cmd_iosched()', 'cmd_oom()', 'cmd_sysrq()',
    'cmd_workqueue()', 'cmd_rcu()', 'cmd_hrtimer()', 'cmd_slab()',
    'cmd_watchdog()', 'cmd_crypto()', 'cmd_cpufreq()', 'cmd_cpuidle()',
    'cmd_regmap()', 'cmd_hwmon()', 'cmd_ftrace()', 'cmd_dmabuf()',
    'cmd_rtc()', 'cmd_pinctrl()', 'cmd_gpio()', 'cmd_i2c()', 'cmd_spi()',
    'cmd_clk()', 'cmd_dmaengine()', 'cmd_mfd()', 'cmd_devtmpfs()',
    'cmd_sysfs()', 'cmd_netns()', 'cmd_netfilter()', 'cmd_seccomp()',
    'cmd_apparmor()', 'cmd_keyring()', 'cmd_audit()',
    'cmd_vmalloc()', 'cmd_percpu()', 'cmd_kfence()', 'cmd_debugobj()',
    'cmd_lockdep()', 'cmd_irqdomain()', 'cmd_namespace()',
    'cmd_tracepoint()', 'cmd_uprobe()', 'cmd_kmod()', 'cmd_firmware()',
    'cmd_remoteproc()', 'cmd_rpmsg()', 'cmd_virtio()', 'cmd_led()',
    'cmd_pwm()', 'cmd_iio()',
    'cmd_free()', 'cmd_meminfo()',  # void(void) per cmd_mem.h
]

# These calls already have () but should have (arg) for commands in cmd_all.c
# Actually the issue is shell.c has BOTH patterns:
# cmd_mem() AND cmd_mem(arg) - depending on the call site
# Let me just NOT change those that have () in shell.c

# Strategy: For each command, look at how it's called in shell.c
# If the call has () but the function takes (arg), change to (arg)
# If the call has (arg) but the function takes (), change to ()

import re

# First, find all calls in shell.c
calls = {}
for m in re.finditer(r'\b(cmd_\w+)\s*\(([^)]*)\)', content):
    fn = m.group(1)
    args = m.group(2).strip()
    if fn not in calls:
        calls[fn] = set()
    calls[fn].add(args)

print('Calls with potential signature issues:')
problem_fns = []
for fn, args in calls.items():
    # Skip games, app functions
    if fn in ['cmd_life', 'cmd_sokoban', 'cmd_typing', 'cmd_ascii', 'cmd_nano', 'cmd_edit']:
        continue
    # Multiple patterns but at least one is empty
    if '' in args:
        problem_fns.append(fn)

print(f'{len(problem_fns)} functions called with no args')
for fn in sorted(problem_fns):
    print(f'  {fn}: {calls[fn]}')