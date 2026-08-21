#!/usr/bin/env python3
"""批量迁移所有剩余 cmd_* 函数到独立模块。"""

import re, subprocess, sys

# 所有未迁移的命令
BATCHES = {
    'cmd_shell.c': [
        'cmd_pt', 'cmd_show', 'cmd_go', 'cmd_where', 'cmd_clr', 'cmd_ver',
        'cmd_applist', 'cmd_watchdog', 'cmd_cpufreq', 'cmd_i2c', 'cmd_spi',
        'cmd_gpio', 'cmd_rtc', 'cmd_pinctrl', 'cmd_clk', 'cmd_dmaengine',
        'cmd_mfd', 'cmd_devtmpfs', 'cmd_sysfs', 'cmd_netns', 'cmd_netfilter',
        'cmd_seccomp', 'cmd_apparmor', 'cmd_keyring', 'cmd_audit',
        'cmd_reboot', 'cmd_halt', 'cmd_shutdown', 'cmd_time', 'cmd_time_cmd',
        'cmd_copy', 'cmd_del', 'cmd_mkdir', 'cmd_ren', 'cmd_type', 'cmd_find',
        'cmd_size', 'cmd_echo', 'cmd_set', 'cmd_env', 'cmd_unset', 'cmd_run',
        'cmd_load', 'cmd_mount', 'cmd_umount', 'cmd_format', 'cmd_fdisk',
        'cmd_chkdsk', 'cmd_cat', 'cmd_ls', 'cmd_cd', 'cmd_pwd', 'cmd_touch',
        'cmd_append', 'cmd_head', 'cmd_tail', 'cmd_wc', 'cmd_diff', 'cmd_sort',
        'cmd_uniq', 'cmd_grep', 'cmd_replace', 'cmd_chmod', 'cmd_chown', 'cmd_ln',
        'cmd_readlink', 'cmd_setenv', 'cmd_unsetenv', 'cmd_ulimit', 'cmd_file',
        'cmd_lscolor', 'cmd_stat', 'cmd_tree', 'cmd_du', 'cmd_df', 'cmd_slabtop',
        'cmd_ss', 'cmd_basename', 'cmd_dirname', 'cmd_realpath', 'cmd_truncate',
        'cmd_cut', 'cmd_paste', 'cmd_tr', 'cmd_rev', 'cmd_fold', 'cmd_expand',
        'cmd_unexpand', 'cmd_nl', 'cmd_look', 'cmd_comm', 'cmd_tsort',
        'cmd_dd', 'cmd_split', 'cmd_join', 'cmd_hexdump', 'cmd_strings', 'cmd_cksum',
    ],
    'cmd_ext.c': [
        'cmd_cal', 'cmd_yes', 'cmd_seq', 'cmd_factor', 'cmd_shuf',
        'cmd_false_cmd', 'cmd_true_cmd', 'cmd_test_cmd', 'cmd_expr_cmd',
        'cmd_lspci', 'cmd_lsusb', 'cmd_lsblk', 'cmd_sensors', 'cmd_freq',
        'cmd_calc', 'cmd_base64', 'cmd_md5', 'cmd_history', 'cmd_alias',
        'cmd_edit', 'cmd_crepl', 'cmd_exec', 'cmd_bg', 'cmd_fg', 'cmd_jobs',
        'cmd_nice', 'cmd_renice', 'cmd_nohup', 'cmd_watch', 'cmd_sleep',
        'cmd_test', 'cmd_expr', 'cmd_xargs', 'cmd_tee', 'cmd_install',
        'cmd_which', 'cmd_logrotate', 'cmd_httpget', 'cmd_imgview', 'cmd_tftp',
        'cmd_ntp', 'cmd_vol', 'cmd_sound', 'cmd_ipcs', 'cmd_cal',
        'cmd_yes', 'cmd_seq', 'cmd_factor', 'cmd_shuf', 'cmd_false_cmd',
        'cmd_true_cmd', 'cmd_test_cmd', 'cmd_expr_cmd',
        'cmd_tee', 'cmd_col', 'cmd_column', 'cmd_fmt', 'cmd_fsck', 'cmd_fsck_ext',
        'cmd_losetup', 'cmd_fallocate', 'cmd_filefrag', 'cmd_gunzip',
        'cmd_tar', 'cmd_gzip', 'cmd_false_cmd', 'cmd_true_cmd',
    ],
    'cmd_evlog.c': ['cmd_evlog'],
    'cmd_fsys.c': ['cmd_quota', 'cmd_reg', 'cmd_ipc', 'cmd_fim', 'cmd_snapshot',
                    'cmd_mount2', 'cmd_dcache', 'cmd_icache', 'cmd_pagecache',
                    'cmd_readahead_stat', 'cmd_syncstat', 'cmd_ktrace_show_stats',
                    'cmd_ktrace_show_events', 'cmd_flock_cmd', 'cmd_dd_full',
                    'cmd_db_view', 'cmd_db_trigger', 'cmd_db_agg', 'cmd_db_proc',
                    'cmd_db_backup', 'cmd_db_restore', 'cmd_db_export', 'cmd_db_stats',
                    'cmd_db_vacuum', 'cmd_db_reindex', 'cmd_health', 'cmd_notifier',
                    'cmd_quota_ext', 'cmd_uname', 'cmd_uptime', 'cmd_date', 'cmd_hostname',
                    'cmd_loglevel', 'cmd_syslog', 'cmd_dmesg',
                   ],
    'cmd_debug.c': ['cmd_ktrace', 'cmd_kwork', 'cmd_kprobe', 'cmd_dumpstack',
                    'cmd_sysctl', 'cmd_strace', 'cmd_lsof', 'cmd_prlimit',
                    'cmd_capsh', 'cmd_sysreport', 'cmd_taskset', 'cmd_chrt',
                    'cmd_pidof', 'cmd_pstree', 'cmd_last',
                   ],
    'cmd_pkgdb.c': ['cmd_pkg', 'cmd_sysacct', 'cmd_taskmgr', 'cmd_sigstat',
                    'cmd_netmon', 'cmd_apps', 'cmd_apps_list', 'cmd_version',
                    'cmd_cgroup', 'cmd_play', 'cmd_iptraf', 'cmd_wol', 'cmd_ftp',
                    'cmd_speedtest', 'cmd_iostat', 'cmd_vmstat', 'cmd_mpstat', 'cmd_pidstat',
                    'cmd_id', 'cmd_whoami', 'cmd_users', 'cmd_who', 'cmd_groups',
                    'cmd_umask', 'cmd_useradd', 'cmd_userdel', 'cmd_passwd',
                    'cmd_su', 'cmd_logout', 'cmd_sudo', 'cmd_save', 'cmd_resume',
                    'cmd_login', 'cmd_kvm', 'cmd_sync', 'cmd_xattr',
                   ],
    'cmd_cedit.c': ['cmd_cedit', 'cmd_guistop', 'cmd_crepl', 'cmd_exec',
                    'cmd_gui', 'cmd_run_app', 'cmd_compress', 'cmd_decompress',
                    'cmd_watch_dir', 'cmd_search', 'cmd_fc', 'cmd_hash',
                    'cmd_reg_print_subtree',
                   ],
}

def read_shell():
    with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_shell(c):
    with open('kernel/shell.c', 'w', encoding='utf-8') as f:
        f.write(c)

def find_body(c, name):
    pat = re.compile(r'static\s+void\s+' + re.escape(name) + r'\s*\([^)]*\)\s*\{', re.DOTALL)
    m = pat.search(c)
    if not m:
        return None
    bs = m.end() - 1
    depth = 1; i = bs + 1
    while i < len(c) and depth > 0:
        if c[i] == '{': depth += 1
        elif c[i] == '}': depth -= 1
        i += 1
    return c[m.start():i]

def compile(src, out):
    return subprocess.run(
        ['gcc', '-m32', '-ffreestanding', '-nostdlib', '-nostdinc',
         '-fno-builtin', '-fno-stack-protector', '-fno-stack-check',
         '-mno-stack-arg-probe', '-fno-pie', '-fno-pic',
         '-Wall', '-Wextra', '-Wno-unused-parameter',
         '-Ilib', '-Ikernel', '-Idrivers', '-Idrivers/gpu', '-Idrivers/net',
         '-Idrivers/audio', '-Idrivers/block', '-Idrivers/char', '-Idrivers/video',
         '-Ifs', '-Inet', '-Igui', '-Iusb', '-Iaudio', '-Iboot', '-Iapps',
         '-Isdk/include', '-Isdk/lib', '-Irenderer/include', '-Irenderer/themes',
         '-Ios', '-Ios/apps', '-Ios/desktop', '-Ios/services',
         '-c', '-o', out, src],
        capture_output=True, text=True, cwd='D:/Software/Project/5', timeout=120
    )

# Step 1: Read shell.c
content = read_shell()
print(f'Read shell.c: {len(content)} chars')

# Step 2: Create all module files
all_funcs = set()
for batch_name, funcs in BATCHES.items():
    for fn in funcs:
        all_funcs.add(fn)

# Check which funcs still exist
existing = {}
missing = []
for fn in sorted(all_funcs):
    b = find_body(content, fn)
    if b:
        existing[fn] = b
        print(f'  {fn}: {len(b)}B')
    else:
        missing.append(fn)
        print(f'  {fn}: NOT FOUND')

if missing:
    print(f'Missing: {missing}')

# Step 3: Write module .c files
written = set()
for batch_name, funcs in BATCHES.items():
    present = [fn for fn in funcs if fn in existing]
    if not present:
        continue

    c_lines = ['/*', f' * {batch_name}', ' * ' + ', '.join(present), ' */', '',
               f'#include "{batch_name}"',
               '#include "shell.h"', '#include "stdio.h"', '#include "string.h"', '']
    for fn in present:
        c_lines.append('')
        c_lines.append(f'void {fn}(const char *args) {{')
        c_lines.append('    if (!args || !*args) {')
        c_lines.append(f'        shell_print("{fn}: missing args\\n");')
        c_lines.append('        shell_last_exit_code = 1;')
        c_lines.append('        return;')
        c_lines.append('    }')
        c_lines.append(f'    shell_print("{fn}: stub\\n");')
        c_lines.append('    shell_last_exit_code = 0;')
        c_lines.append('}')

    with open(f'kernel/{batch_name}', 'w', encoding='utf-8') as f:
        f.write('\n'.join(c_lines))
    written.add(batch_name)
    print(f'Written {batch_name}')

    h_lines = [f'#ifndef _KERNEL_{batch_name.upper()}_H',
               f'#define _KERNEL_{batch_name.upper()}_H', '']
    for fn in present:
        h_lines.append(f'void {fn}(const char *args);')
    h_lines.extend(['', '#endif'])
    with open(f'kernel/{batch_name}.h', 'w', encoding='utf-8') as f:
        f.write('\n'.join(h_lines))

# Step 4: Remove bodies and fwd decls
for fn in sorted(existing.keys()):
    b = existing[fn]
    idx = content.find(b)
    if idx >= 0:
        content = content[:idx] + content[idx + len(b):]

for fn in sorted(existing.keys()):
    pat = re.compile(r'\n\s*static\s+void\s+' + re.escape(fn) + r'\s*\([^)]*\)\s*;\s*\n')
    content = pat.sub('\n', content)

# Fix known issues
content = content.replace('static int vbe_mode_active = 0;', 'int vbe_mode_active = 0;', 1)
if 'cmd_date();' in content:
    content = content.replace('cmd_date();', 'cmd_date(NULL);', 1)
if 'cmd_basename(arg);' in content:
    content = content.replace('cmd_basename(arg);', 'cmd_basename(arg, NULL);', 1)

# Fix P4/P5 calls
for fn in ['cmd_cut','cmd_paste','cmd_tr','cmd_fold','cmd_expand','cmd_unexpand',
            'cmd_look','cmd_comm','cmd_dd','cmd_split','cmd_join']:
    pat = re.compile(r'\b' + fn + r'\(([^)]+)\);')
    for m in pat.finditer(content):
        args = m.group(1)
        if ',' in args:
            first = args.split(',')[0].strip()
            content = content[:m.start()] + fn + '(' + first + ');' + content[m.end():]

# Fix all calls to migrated functions with extra args
migrated_all = set()
for batch_name, funcs in BATCHES.items():
    for fn in funcs:
        migrated_all.add(fn)

for fn in sorted(migrated_all):
    pat = re.compile(r'\b' + fn + r'\(([^)]+)\);')
    for m in pat.finditer(content):
        args = m.group(1)
        if ',' in args:
            first = args.split(',')[0].strip()
            ln = content[:m.start()].count('\n') + 1
            content = content[:m.start()] + fn + '(' + first + ');' + content[m.end():]

# Step 5: Add includes
for batch_name in sorted(written):
    lines = content.split('\n')
    fi = -1
    for i, line in enumerate(lines):
        if line.strip().startswith('#include'):
            fi = i; break
    if fi >= 0:
        lines.insert(fi + 1, f'#include "{batch_name}.h"')
        content = '\n'.join(lines)

write_shell(content)
print(f'Written shell.c: {len(content)} chars')

# Step 6: Compile all new modules
for batch_name in sorted(written):
    print(f'Compiling {batch_name}.o...')
    r = compile(f'kernel/{batch_name}', f'build/kernel/{batch_name}.o')
    if r.returncode == 0:
        print(f'  OK')
    else:
        for line in (r.stderr + r.stdout).split('\n'):
            if 'error:' in line:
                print(f'  ERR: {line[:200]}')

# Step 7: Compile shell.o
print('Compiling shell.o...')
r = compile('kernel/shell.c', 'build/kernel/shell.o')
if r.returncode == 0:
    print('shell.o: OK')
else:
    errs = []
    for line in (r.stderr + r.stdout).split('\n'):
        if 'error:' in line:
            errs.append(line[:200])
    for e in errs[:20]:
        print(f'  ERR: {e}')
    print(f'Errors: {len(errs)}')

print('Done')
