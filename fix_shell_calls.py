#!/usr/bin/env python3
"""Fix shell.c call sites to match actual module signatures"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

print(f'Before: {len(content)} chars')

# Fix calls to commands that take NO args
# Based on existing module headers: void(void)
no_arg_cmds = [
    'cmd_ifconfig', 'cmd_route', 'cmd_netstat', 'cmd_arp', 'cmd_lanscan',
    'cmd_sockstat', 'cmd_free', 'cmd_meminfo', 'cmd_ps', 'cmd_top',
    'cmd_sysinfo', 'cmd_uptime', 'cmd_ipcs', 'cmd_slabtop', 'cmd_sync',
    'cmd_pwd', 'cmd_df', 'cmd_netmon', 'cmd_sigstat',
    'cmd_false_cmd', 'cmd_true_cmd',
    # These were incorrectly given arg in shell.c
    'cmd_ifconfig', 'cmd_route', 'cmd_netstat', 'cmd_arp',
    'cmd_lanscan', 'cmd_sockstat', 'cmd_ping',
]

# These take 1 arg from shell.c perspective (arg or NULL)
one_arg_cmds = [
    'cmd_mem', 'cmd_env', 'cmd_dmesg', 'cmd_loglevel', 'cmd_syslog',
    'cmd_mount', 'cmd_umount', 'cmd_format', 'cmd_fdisk', 'cmd_chkdsk',
    'cmd_cat', 'cmd_ls', 'cmd_cd', 'cmd_touch', 'cmd_head', 'cmd_tail',
    'cmd_wc', 'cmd_diff', 'cmd_sort', 'cmd_uniq', 'cmd_col', 'cmd_column',
    'cmd_grep', 'cmd_replace', 'cmd_chmod', 'cmd_chown', 'cmd_file',
    'cmd_lscolor', 'cmd_ln', 'cmd_readlink', 'cmd_stat', 'cmd_tree',
    'cmd_du', 'cmd_mkfifo', 'cmd_mknod', 'cmd_ulimit', 'cmd_losetup',
    'cmd_fallocate', 'cmd_filefrag', 'cmd_fsck', 'cmd_fsck_ext',
    'cmd_history', 'cmd_alias', 'cmd_base64', 'cmd_md5', 'cmd_hash',
    'cmd_compress', 'cmd_decompress', 'cmd_watch', 'cmd_watch_dir',
    'cmd_calc', 'cmd_freq', 'cmd_sensors', 'cmd_lspci', 'cmd_lsusb',
    'cmd_lsblk', 'cmd_vol', 'cmd_play', 'cmd_sound',
    'cmd_edit', 'cmd_imgview', 'cmd_httpget', 'cmd_wget',
    'cmd_ntp', 'cmd_tftp', 'cmd_nslookup', 'cmd_dig', 'cmd_dhcp',
    'cmd_tcpdump', 'cmd_traceroute', 'cmd_nmap', 'cmd_mtr',
    'cmd_fw', 'cmd_nc',
    'cmd_service', 'cmd_crontab',
    'cmd_logrotate', 'cmd_logrotate_ext',
    'cmd_version', 'cmd_apps', 'cmd_apps_list',
    'cmd_taskmgr', 'cmd_pkg', 'cmd_sysacct',
    'cmd_evlog', 'cmd_quota', 'cmd_quota_ext',
    'cmd_reg', 'cmd_ipc', 'cmd_fim', 'cmd_snapshot',
    'cmd_mount2', 'cmd_dcache', 'cmd_icache', 'cmd_pagecache',
    'cmd_readahead_stat', 'cmd_syncstat', 'cmd_fsstat', 'cmd_flock',
    'cmd_flock_cmd', 'cmd_dd_full',
    'cmd_db', 'cmd_db_view', 'cmd_db_trigger', 'cmd_db_agg',
    'cmd_db_stats', 'cmd_db_export', 'cmd_db_backup', 'cmd_db_restore',
    'cmd_db_vacuum', 'cmd_db_reindex', 'cmd_db_proc',
    'cmd_cgroup', 'cmd_sysacct',
    'cmd_ktrace', 'cmd_kwork', 'cmd_kprobe', 'cmd_dumpstack',
    'cmd_sysctl', 'cmd_strace', 'cmd_lsof', 'cmd_prlimit',
    'cmd_capsh', 'cmd_sysreport', 'cmd_taskset', 'cmd_chrt',
    'cmd_pidof', 'cmd_pstree', 'cmd_last', 'cmd_nice', 'cmd_renice',
    'cmd_notifier',
    'cmd_kvm', 'cmd_namespace', 'cmd_tracepoint', 'cmd_uprobe',
    'cmd_kmod', 'cmd_firmware', 'cmd_remoteproc', 'cmd_rpmsg',
    'cmd_virtio', 'cmd_vmalloc', 'cmd_percpu', 'cmd_kfence',
    'cmd_debugobj', 'cmd_lockdep', 'cmd_irqdomain', 'cmd_regmap',
    'cmd_hwmon', 'cmd_ftrace', 'cmd_dmabuf', 'cmd_softirq',
    'cmd_iosched', 'cmd_oom', 'cmd_sysrq', 'cmd_workqueue',
    'cmd_rcu', 'cmd_hrtimer', 'cmd_slab', 'cmd_cpuidle',
    'cmd_crypto', 'cmd_firmware', 'cmd_watchdog',
    'cmd_cpufreq', 'cmd_cpuidle', 'cmd_led', 'cmd_pwm',
    'cmd_iio', 'cmd_dmaengine', 'cmd_clk', 'cmd_pinctrl',
    'cmd_gpio', 'cmd_rtc', 'cmd_i2c', 'cmd_spi', 'cmd_mfd',
    'cmd_devtmpfs', 'cmd_sysfs', 'cmd_netns', 'cmd_netfilter',
    'cmd_seccomp', 'cmd_apparmor', 'cmd_keyring', 'cmd_audit',
    'cmd_users', 'cmd_groups', 'cmd_umask',
]

fixed = 0
for cmd in no_arg_cmds:
    old = f'{cmd}(arg)'
    new = f'{cmd}()'
    if old in content:
        content = content.replace(old, new)
        print(f'  {cmd}(arg) -> {cmd}()')
        fixed += 1

print(f'Fixed {fixed} calls')

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(content)
print(f'After: {len(content)} chars')
