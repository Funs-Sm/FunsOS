#!/usr/bin/env python3
"""Add ALL missing cmd_* declarations with correct signatures"""
import re

# Known signatures for commands in cmd_all.c
sigs = {
    'cmd_ipcs': 'void(void)',
    'cmd_slabtop': 'void(void)',
    'cmd_sync': 'void(void)',
    'cmd_pwd': 'void(void)',
    'cmd_df': 'void(void)',
    'cmd_netmon': 'void(void)',
    'cmd_sigstat': 'void(void)',
    'cmd_httpget': 'void(const char*, const char*)',
    'cmd_ss': 'void(const char*)',
    'cmd_mem': 'void(const char*)',  # not in module
    'cmd_evlog': 'void(const char*)',
    'cmd_quota': 'void(const char*)',
    'cmd_quota_ext': 'void(const char*)',
    'cmd_fim': 'void(const char*)',
    'cmd_snapshot': 'void(const char*)',
    'cmd_mount2': 'void(const char*)',
    'cmd_dcache': 'void(const char*)',
    'cmd_icache': 'void(const char*)',
    'cmd_pagecache': 'void(const char*)',
    'cmd_readahead_stat': 'void(const char*)',
    'cmd_syncstat': 'void(const char*)',
    'cmd_kwork': 'void(const char*)',
    'cmd_ktrace': 'void(const char*)',
    'cmd_kprobe': 'void(const char*)',
    'cmd_dumpstack': 'void(void)',
    'cmd_strace': 'void(const char*)',
    'cmd_lsof': 'void(void)',
    'cmd_prlimit': 'void(const char*)',
    'cmd_capsh': 'void(void)',
    'cmd_sysreport': 'void(const char*)',
    'cmd_notifier': 'void(const char*)',
    'cmd_fsstat': 'void(void)',
    'cmd_flock': 'void(const char*)',
    'cmd_flock_cmd': 'void(const char*)',
    'cmd_dd_full': 'void(const char*)',
    'cmd_ipc': 'void(const char*)',
    'cmd_sysctl': 'void(const char*)',
    'cmd_dmesg': 'void(const char*)',
    'cmd_loglevel': 'void(const char*)',
    'cmd_syslog': 'void(const char*)',
    'cmd_dns': 'void(const char*)',
    'cmd_telnet': 'void(const char*)',
    'cmd_fw': 'void(const char*)',
    'cmd_tftp': 'void(const char*)',
    'cmd_ntp': 'void(const char*)',
    'cmd_nslookup': 'void(const char*)',
    'cmd_dig': 'void(const char*)',
    'cmd_dhcp': 'void(const char*)',
    'cmd_tcpdump': 'void(const char*)',
    'cmd_traceroute': 'void(const char*)',
    'cmd_nmap': 'void(const char*)',
    'cmd_mtr': 'void(const char*)',
    'cmd_softirq': 'void(void)',
    'cmd_iosched': 'void(void)',
    'cmd_oom': 'void(void)',
    'cmd_sysrq': 'void(void)',
    'cmd_workqueue': 'void(void)',
    'cmd_rcu': 'void(void)',
    'cmd_hrtimer': 'void(void)',
    'cmd_slab': 'void(void)',
    'cmd_watchdog': 'void(void)',
    'cmd_crypto': 'void(void)',
    'cmd_cpufreq': 'void(void)',
    'cmd_cpuidle': 'void(void)',
    'cmd_regmap': 'void(void)',
    'cmd_hwmon': 'void(void)',
    'cmd_ftrace': 'void(void)',
    'cmd_dmabuf': 'void(void)',
    'cmd_rtc': 'void(void)',
    'cmd_pinctrl': 'void(void)',
    'cmd_gpio': 'void(void)',
    'cmd_i2c': 'void(void)',
    'cmd_spi': 'void(void)',
    'cmd_clk': 'void(void)',
    'cmd_dmaengine': 'void(void)',
    'cmd_mfd': 'void(void)',
    'cmd_devtmpfs': 'void(void)',
    'cmd_sysfs': 'void(void)',
    'cmd_netns': 'void(void)',
    'cmd_netfilter': 'void(void)',
    'cmd_seccomp': 'void(void)',
    'cmd_apparmor': 'void(void)',
    'cmd_keyring': 'void(void)',
    'cmd_audit': 'void(void)',
    'cmd_vmalloc': 'void(void)',
    'cmd_percpu': 'void(void)',
    'cmd_kfence': 'void(void)',
    'cmd_debugobj': 'void(void)',
    'cmd_lockdep': 'void(void)',
    'cmd_irqdomain': 'void(void)',
    'cmd_namespace': 'void(void)',
    'cmd_tracepoint': 'void(void)',
    'cmd_uprobe': 'void(void)',
    'cmd_kmod': 'void(void)',
    'cmd_firmware': 'void(void)',
    'cmd_remoteproc': 'void(void)',
    'cmd_rpmsg': 'void(void)',
    'cmd_virtio': 'void(void)',
    'cmd_led': 'void(void)',
    'cmd_pwm': 'void(void)',
    'cmd_iio': 'void(void)',
}

def to_decl(fn, sig):
    if sig == 'void(void)':
        return f'extern void {fn}(void);'
    elif sig == 'void(const char*)':
        return f'extern void {fn}(const char *arg);'
    elif sig == 'void(const char*, const char*)':
        return f'extern void {fn}(const char *a, const char *b);'
    else:
        return f'extern void {fn}(const char *arg);'

with open('kernel/cmd_all.h', 'r') as f:
    header = f.read()

# Find existing declarations
existing = set()
for m in re.finditer(r'\b(cmd_\w+)\b', header):
    existing.add(m.group(1))
existing -= {'shell_print', 'shell_last_exit_code', 'vbe_mode_active'}

new_decls = []
for fn, sig in sorted(sigs.items()):
    if fn not in existing:
        new_decls.append(to_decl(fn, sig))

print(f'Adding {len(new_decls)} new declarations')

if new_decls:
    header = header.replace('/* shell.c definitions */', '\n'.join(new_decls) + '\n\n/* shell.c definitions */')
    with open('kernel/cmd_all.h', 'w') as f:
        f.write(header)
    print('Updated!')