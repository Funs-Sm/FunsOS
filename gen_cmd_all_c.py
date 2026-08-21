#!/usr/bin/env python3
"""Generate clean cmd_all.c from actual module header signatures"""
import re, os

# Known signatures from module headers
module_sigs = {
    'cmd_ifconfig': 'void(void)',
    'cmd_route': 'void(void)',
    'cmd_netstat': 'void(void)',
    'cmd_arp': 'void(void)',
    'cmd_lanscan': 'void(void)',
    'cmd_sockstat': 'void(void)',
    'cmd_nc': 'void(const char*, const char*)',
    'cmd_httpget': 'void(const char*, const char*)',
    'cmd_wget': 'void(const char*, const char*)',
    'cmd_ping': 'void(const char*)',
    'cmd_nslookup': 'void(const char*)',
    'cmd_dig': 'void(const char*)',
    'cmd_dhcp': 'void(const char*)',
    'cmd_tcpdump': 'void(const char*)',
    'cmd_traceroute': 'void(const char*)',
    'cmd_nmap': 'void(const char*)',
    'cmd_mtr': 'void(const char*)',
    'cmd_dns': 'void(const char*)',
    'cmd_telnet': 'void(const char*)',
    'cmd_fw': 'void(const char*)',
    'cmd_ss': 'void(const char*)',
    'cmd_iptables': 'void(const char*)',
    'cmd_ipcs': 'void(void)',
    'cmd_free': 'void(void)',
    'cmd_meminfo': 'void(void)',
    'cmd_ps': 'void(void)',
    'cmd_top': 'void(void)',
    'cmd_sysinfo': 'void(void)',
    'cmd_uptime': 'void(void)',
    'cmd_date': 'void(const char*)',
    'cmd_hostname': 'void(const char*)',
    'cmd_service': 'void(const char*, const char*, const char*)',
    'cmd_crontab': 'void(const char*, const char*, const char*)',
    'cmd_cal': 'void(const char*)',
    'cmd_yes': 'void(const char*)',
    'cmd_seq': 'void(const char*, const char*)',
    'cmd_factor': 'void(const char*)',
    'cmd_shuf': 'void(const char*)',
    'cmd_false_cmd': 'void(void)',
    'cmd_true_cmd': 'void(void)',
    'cmd_test_cmd': 'void(const char*)',
    'cmd_expr_cmd': 'void(const char*)',
    'cmd_uname': 'void(const char*)',
    'cmd_basename': 'void(const char*, const char*)',
    'cmd_dirname': 'void(const char*)',
    'cmd_realpath': 'void(const char*)',
    'cmd_truncate': 'void(const char*, const char*)',
    'cmd_cut': 'void(const char*)',
    'cmd_paste': 'void(const char*)',
    'cmd_tr': 'void(const char*)',
    'cmd_rev': 'void(const char*)',
    'cmd_fold': 'void(const char*)',
    'cmd_expand': 'void(const char*)',
    'cmd_unexpand': 'void(const char*)',
    'cmd_nl': 'void(const char*)',
    'cmd_look': 'void(const char*)',
    'cmd_comm': 'void(const char*)',
    'cmd_tsort': 'void(const char*)',
    'cmd_dd': 'void(const char*)',
    'cmd_split': 'void(const char*)',
    'cmd_join': 'void(const char*)',
    'cmd_hexdump': 'void(const char*)',
    'cmd_strings': 'void(const char*)',
    'cmd_cksum': 'void(const char*)',
    'cmd_tar': 'void(const char*)',
    'cmd_gzip': 'void(const char*)',
    'cmd_gunzip': 'void(const char*)',
    'cmd_iptraf': 'void(const char*)',
    'cmd_wol': 'void(const char*)',
    'cmd_ftp': 'void(const char*)',
    'cmd_speedtest': 'void(const char*)',
    'cmd_iostat': 'void(const char*)',
    'cmd_vmstat': 'void(const char*)',
    'cmd_mpstat': 'void(const char*)',
    'cmd_pidstat': 'void(const char*)',
    'cmd_id': 'void(const char*)',
    'cmd_whoami': 'void(const char*)',
    'cmd_users': 'void(const char*)',
    'cmd_who': 'void(const char*)',
    'cmd_groups': 'void(const char*)',
    'cmd_umask': 'void(const char*)',
    'cmd_dmesg': 'void(const char*)',
    'cmd_loglevel': 'void(const char*)',
    'cmd_syslog': 'void(const char*)',
}

def stub_code(fn, sig):
    if sig == 'void(void)':
        return f'void {fn}(void) {{ shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}\n'
    elif sig == 'void(const char*)':
        return f'void {fn}(const char *arg) {{ (void)arg; shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}\n'
    elif sig == 'void(const char*, const char*)':
        return f'void {fn}(const char *a, const char *b) {{ (void)a; (void)b; shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}\n'
    elif sig == 'void(const char*, const char*, const char*)':
        return f'void {fn}(const char *a, const char *b, const char *c) {{ (void)a; (void)b; (void)c; shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}\n'
    else:
        return f'void {fn}(const char *arg) {{ (void)arg; shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}\n'

lines = ['/* cmd_all.c - Auto-generated stub implementations */', '#include "cmd_all.h"', '']
lines.append('/* Games / Apps */')
lines.append('int cmd_life(int argc, char *argv[]) { (void)argc; (void)argv; shell_print("Life: stub\\n"); return 0; }')
lines.append('int cmd_sokoban(int argc, char *argv[]) { (void)argc; (void)argv; shell_print("Sokoban: stub\\n"); return 0; }')
lines.append('int cmd_typing(int argc, char *argv[]) { (void)argc; (void)argv; shell_print("Typing: stub\\n"); return 0; }')
lines.append('int cmd_ascii(int argc, char *argv[]) { (void)argc; (void)argv; shell_print("ASCII: stub\\n"); return 0; }')
lines.append('int cmd_nano(int argc, char *argv[]) { (void)argc; (void)argv; shell_print("nano: stub\\n"); return 0; }')
lines.append('')
lines.append('/* cmd_edit */')
lines.append('void cmd_edit(const char *path) { (void)path; shell_print("edit: stub\\n"); }')
lines.append('')
lines.append('/* Stubs matching module header signatures */')
for fn, sig in sorted(module_sigs.items()):
    lines.append(stub_code(fn, sig))

lines.append('')
lines.append('/* Stubs for commands not in any module header */')
remaining = [
    'cmd_pt', 'cmd_show', 'cmd_go', 'cmd_where', 'cmd_clr', 'cmd_ver', 'cmd_help',
    'cmd_schedpolicy', 'cmd_mempolicy', 'cmd_reboot', 'cmd_halt', 'cmd_shutdown',
    'cmd_time_cmd', 'cmd_time', 'cmd_mem', 'cmd_dev', 'cmd_copy', 'cmd_del',
    'cmd_mkdir', 'cmd_ren', 'cmd_type', 'cmd_find', 'cmd_size', 'cmd_echo',
    'cmd_set', 'cmd_unset', 'cmd_run', 'cmd_load', 'cmd_kill', 'cmd_append',
    'cmd_setenv', 'cmd_unsetenv', 'cmd_bg', 'cmd_fg', 'cmd_jobs', 'cmd_nice',
    'cmd_renice', 'cmd_nohup', 'cmd_watch', 'cmd_sleep', 'cmd_xargs', 'cmd_tee',
    'cmd_install', 'cmd_which', 'cmd_logrotate', 'cmd_logrotate_ext', 'cmd_imgview',
    'cmd_vol', 'cmd_sound', 'cmd_guistop', 'cmd_crepl', 'cmd_exec', 'cmd_gui',
    'cmd_run_app', 'cmd_taskbar', 'cmd_search', 'cmd_fc', 'cmd_hash', 'cmd_save',
    'cmd_resume', 'cmd_logout', 'cmd_test', 'cmd_expr', 'cmd_pidof', 'cmd_pstree',
    'cmd_last', 'cmd_taskset', 'cmd_chrt', 'cmd_strace', 'cmd_lsof', 'cmd_prlimit',
    'cmd_capsh', 'cmd_sysreport', 'cmd_dumpstack', 'cmd_kwork', 'cmd_ktrace',
    'cmd_kprobe', 'cmd_notifier', 'cmd_sysctl', 'cmd_losetup', 'cmd_fallocate',
    'cmd_filefrag', 'cmd_fsck', 'cmd_fsck_ext', 'cmd_sensors', 'cmd_cpufreq',
    'cmd_i2c', 'cmd_spi', 'cmd_gpio', 'cmd_rtc', 'cmd_pinctrl', 'cmd_clk',
    'cmd_dmaengine', 'cmd_mfd', 'cmd_devtmpfs', 'cmd_sysfs', 'cmd_netns',
    'cmd_netfilter', 'cmd_seccomp', 'cmd_apparmor', 'cmd_keyring', 'cmd_audit',
    'cmd_watchdog', 'cmd_applist', 'cmd_cpuidle', 'cmd_crypto', 'cmd_firmware',
    'cmd_remoteproc', 'cmd_rpmsg', 'cmd_virtio', 'cmd_vmalloc', 'cmd_percpu',
    'cmd_kfence', 'cmd_debugobj', 'cmd_lockdep', 'cmd_irqdomain', 'cmd_regmap',
    'cmd_hwmon', 'cmd_ftrace', 'cmd_dmabuf', 'cmd_namespace', 'cmd_tracepoint',
    'cmd_uprobe', 'cmd_kmod', 'cmd_iio', 'cmd_pwm', 'cmd_led', 'cmd_cat',
    'cmd_ls', 'cmd_cd', 'cmd_touch', 'cmd_head', 'cmd_tail', 'cmd_wc',
    'cmd_diff', 'cmd_sort', 'cmd_uniq', 'cmd_col', 'cmd_column', 'cmd_grep',
    'cmd_replace', 'cmd_chmod', 'cmd_chown', 'cmd_file', 'cmd_lscolor', 'cmd_ln',
    'cmd_readlink', 'cmd_stat', 'cmd_tree', 'cmd_du', 'cmd_mkfifo', 'cmd_mknod',
    'cmd_ulimit', 'cmd_env', 'cmd_slab', 'cmd_slabtop', 'cmd_sync', 'cmd_pwd',
    'cmd_df', 'cmd_ipcs', 'cmd_netmon', 'cmd_sigstat', 'cmd_nslookup',
    'cmd_dig', 'cmd_dhcp', 'cmd_tcpdump', 'cmd_traceroute', 'cmd_nmap',
    'cmd_mtr', 'cmd_dns', 'cmd_telnet', 'cmd_fw', 'cmd_ss', 'cmd_tftp',
    'cmd_ntp', 'cmd_health', 'cmd_fsstat', 'cmd_flock', 'cmd_flock_cmd',
    'cmd_mount', 'cmd_umount', 'cmd_format', 'cmd_fdisk', 'cmd_chkdsk',
    'cmd_mount2', 'cmd_dcache', 'cmd_icache', 'cmd_pagecache', 'cmd_readahead_stat',
    'cmd_syncstat', 'cmd_reg', 'cmd_ipc', 'cmd_fim', 'cmd_snapshot',
    'cmd_quota', 'cmd_quota_ext', 'cmd_evlog', 'cmd_apps', 'cmd_apps_list',
    'cmd_version', 'cmd_pkg', 'cmd_sysacct', 'cmd_taskmgr', 'cmd_db',
    'cmd_db_view', 'cmd_db_trigger', 'cmd_db_agg', 'cmd_db_stats', 'cmd_db_export',
    'cmd_db_backup', 'cmd_db_restore', 'cmd_db_vacuum', 'cmd_db_reindex',
    'cmd_db_proc', 'cmd_cgroup', 'cmd_iosched', 'cmd_softirq', 'cmd_oom',
    'cmd_sysrq', 'cmd_workqueue', 'cmd_rcu', 'cmd_hrtimer', 'cmd_futexinfo',
    'cmd_epollinfo', 'cmd_inotifyinfo', 'cmd_dd_full',
]
# Remove game functions and module sigs
skip = {'cmd_life','cmd_sokoban','cmd_typing','cmd_ascii','cmd_nano'} | set(module_sigs.keys())
for fn in sorted(set(remaining) - skip):
    lines.append(f'void {fn}(const char *arg) {{ (void)arg; shell_print("{fn} (stub)\\n"); shell_last_exit_code = 0; }}')

with open('kernel/cmd_all.c', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))

print(f'Written {len(lines)} lines')
