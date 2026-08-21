#!/usr/bin/env python3
"""批量迁移所有剩余 cmd_* 到独立模块 - 修复版"""
import re, subprocess, os, time

# 分组：每个模块最多~15个函数（避免头文件循环包含）
BATCHES = [
    # P11: 文件操作命令 (~3.7KB each)
    ('cmd_fops.c', [
        'cmd_cpufreq','cmd_i2c','cmd_spi','cmd_gpio','cmd_rtc',
        'cmd_pinctrl','cmd_clk','cmd_dmaengine','cmd_mfd',
        'cmd_devtmpfs','cmd_sysfs','cmd_netns','cmd_netfilter',
        'cmd_seccomp','cmd_apparmor','cmd_keyring','cmd_audit',
        'cmd_watchdog','cmd_applist','cmd_ver',
    ]),
    # P12: 核心文件命令 (~3.5KB each)
    ('cmd_fileops.c', [
        'cmd_touch','cmd_mkdir','cmd_cat','cmd_ls','cmd_cd','cmd_pwd',
        'cmd_copy','cmd_del','cmd_ren','cmd_append','cmd_type',
        'cmd_stat','cmd_file','cmd_du','cmd_df','cmd_tree',
        'cmd_ln','cmd_readlink','cmd_fdisk','cmd_format',
        'cmd_chkdsk','cmd_mount','cmd_umount',
    ]),
    # P13: 系统命令
    ('cmd_sysops.c', [
        'cmd_set','cmd_env','cmd_unset','cmd_setenv','cmd_unsetenv',
        'cmd_run','cmd_load','cmd_history','cmd_alias',
        'cmd_nice','cmd_renice','cmd_nohup','cmd_bg','cmd_fg','cmd_jobs',
        'cmd_sleep','cmd_xargs','cmd_tee','cmd_install',
        'cmd_which','cmd_where','cmd_clr',
    ]),
    # P14: 文本处理
    ('cmd_textproc.c', [
        'cmd_grep','cmd_replace','cmd_sort','cmd_uniq','cmd_wc',
        'cmd_head','cmd_tail','cmd_diff','cmd_fc',
        'cmd_echo','cmd_lscolor',
    ]),
    # P15: 权限与属性
    ('cmd_perm.c', [
        'cmd_chmod','cmd_chown','cmd_chrt','cmd_ulimit',
        'cmd_ulimit','cmd_flock','cmd_flock_cmd',
        'cmd_xattr','cmd_readahead_stat',
    ]),
    # P16: 数据库相关
    ('cmd_database.c', [
        'cmd_db','cmd_db_proc','cmd_db_view','cmd_db_trigger',
        'cmd_db_agg','cmd_db_stats','cmd_db_export',
        'cmd_db_backup','cmd_db_restore','cmd_db_vacuum','cmd_db_reindex',
    ]),
    # P17: 网络/IO监控
    ('cmd_iomon.c', [
        'cmd_ipcs','cmd_syncstat','cmd_slabtop','cmd_ss',
        'cmd_dcache','cmd_icache','cmd_pagecache',
        'cmd_health','cmd_sync','cmd_syncstat',
    ]),
    # P18: 追踪/调试
    ('cmd_trace.c', [
        'cmd_ktrace','cmd_kwork','cmd_kprobe','cmd_dumpstack',
        'cmd_strace','cmd_lsof','cmd_prlimit','cmd_capsh',
        'cmd_sysreport','cmd_sysctl',
    ]),
    # P19: 调度/进程
    ('cmd_sched.c', [
        'cmd_taskset','cmd_pidof','cmd_pstree','cmd_chrt',
        'cmd_last','cmd_nice','cmd_renice',
        'cmd_notifier','cmd_ktrace_show_stats',
        'cmd_ktrace_show_events',
    ]),
    # P20: 杂项工具
    ('cmd_misc.c', [
        'cmd_lspci','cmd_lsusb','cmd_lsblk','cmd_sensors',
        'cmd_freq','cmd_calc','cmd_base64','cmd_md5','cmd_hash',
        'cmd_compress','cmd_decompress','cmd_watch','cmd_watch_dir',
        'cmd_fallocate','cmd_filefrag','cmd_losetup',
    ]),
    # P21: 网络工具
    ('cmd_nettools.c', [
        'cmd_tftp','cmd_httpget','cmd_ntp','cmd_vol',
        'cmd_play','cmd_sound',
    ]),
    # P22: GUI相关
    ('cmd_gui.c', [
        'cmd_cedit','cmd_guistop','cmd_crepl','cmd_exec',
        'cmd_gui','cmd_run_app','cmd_search',
    ]),
    # P23: 大型功能模块
    ('cmd_pkg.c', [
        'cmd_pkg','cmd_apps','cmd_apps_list','cmd_version',
        'cmd_taskmgr','cmd_sigstat','cmd_netmon',
    ]),
    ('cmd_cgroup.c', [
        'cmd_cgroup','cmd_sysacct',
    ]),
    ('cmd_task.c', [
        'cmd_taskmgr','cmd_sigstat','cmd_netmon','cmd_apps',
        'cmd_apps_list','cmd_version',
    ]),
    # P24: 配额/注册表/IPC
    ('cmd_acct.c', [
        'cmd_quota','cmd_quota_ext','cmd_reg',
        'cmd_ipc','cmd_fim','cmd_snapshot','cmd_mount2',
        'cmd_sysctl',
    ]),
    # P25: 大文件
    ('cmd_big1.c', [
        'cmd_quota','cmd_reg','cmd_ipc',
    ]),
    ('cmd_big2.c', [
        'cmd_taskmgr','cmd_db',
    ]),
    ('cmd_big3.c', [
        'cmd_apps','cmd_cgroup',
    ]),
    ('cmd_big4.c', [
        'cmd_pt','cmd_fim',
    ]),
    ('cmd_big5.c', [
        'cmd_cedit','cmd_ktrace',
    ]),
    ('cmd_big6.c', [
        'cmd_pkg','cmd_sysacct',
    ]),
    ('cmd_big7.c', [
        'cmd_kwork','cmd_snapshot',
    ]),
    ('cmd_big8.c', [
        'cmd_mount2','cmd_kprobe',
    ]),
    ('cmd_big9.c', [
        'cmd_dcache','cmd_icache',
    ]),
    ('cmd_big10.c', [
        'cmd_pagecache','cmd_syncstat',
    ]),
]

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

def write_module(filename, funcs, bodies):
    stub = '// Auto-generated stub module\n'
    stub += f'#include "shell.h"\n'
    stub += f'#include "string.h"\n\n'
    for fn in funcs:
        body = bodies.get(fn)
        if body:
            stub += body + '\n\n'
        else:
            stub += f'void {fn}(const char *arg) {{ shell_print("{fn}: stub\\n"); }}\n\n'
    with open(f'kernel/{filename}', 'w', encoding='utf-8') as f:
        f.write(stub)
    # Header
    h = f'#ifndef _KERNEL_{os.path.splitext(filename)[0].upper()}_H\n#define _KERNEL_{os.path.splitext(filename)[0].upper()}_H\n'
    for fn in funcs:
        h += f'void {fn}(const char *arg);\n'
    h += '#endif\n'
    with open(f'kernel/{filename}.h', 'w', encoding='utf-8') as f:
        f.write(h)
    return filename

def gcc_compile(src, out):
    return subprocess.run(
        ['gcc', '-m32', '-ffreestanding', '-nostdlib', '-nostdinc',
         '-fno-builtin', '-fno-stack-protector', '-fno-stack-check',
         '-mno-stack-arg-probe', '-fno-pie', '-fno-pic',
         '-Wall', '-Wextra', '-Wno-unused-parameter',
         '-D__KERNEL__', '-Ilib', '-I.',
         '-Ilib', '-Ikernel', '-Idrivers', '-Idrivers/gpu', '-Idrivers/net',
         '-Idrivers/audio', '-Idrivers/block', '-Idrivers/char', '-Idrivers/video',
         '-Ifs', '-Inet', '-Igui', '-Iusb', '-Iaudio', '-Iboot', '-Iapps',
         '-Isdk/include', '-Isdk/lib', '-Irenderer/include', '-Irenderer/themes',
         '-Ios', '-Ios/apps', '-Ios/desktop', '-Ios/services',
         '-c', '-o', out, src],
        capture_output=True, text=True, cwd='D:/Software/Project/5', timeout=120
    )

content = read_shell()
print(f'Read shell.c: {len(content)} chars')

# Extract bodies
bodies = {}
for m in re.finditer(r'static\s+void\s+(cmd_\w+)\s*\([^)]*\)\s*\{', content):
    name = m.group(1)
    bs = m.end() - 1
    depth = 1; i = bs + 1
    while i < len(content) and depth > 0:
        if content[i] == '{': depth += 1
        elif content[i] == '}': depth -= 1
        i += 1
    bodies[name] = content[m.start():i]
    print(f'  {name}: {len(bodies[name])}B')

# Write modules
written = []
for batch_name, funcs in BATCHES:
    present = [fn for fn in funcs if fn in bodies]
    if not present:
        continue
    write_module(batch_name, present, bodies)
    written.append(batch_name)
    print(f'Written {batch_name} ({len(present)} funcs)')

# Remove from shell.c
for fn, body in sorted(bodies.items()):
    idx = content.find(body)
    if idx >= 0:
        content = content[:idx] + content[idx + len(body):]

# Remove fwd decls
for fn in sorted(bodies.keys()):
    pat = re.compile(r'\n\s*static\s+void\s+' + re.escape(fn) + r'\s*\([^)]*\)\s*;\s*\n')
    new_c = pat.sub('\n', content)
    if new_c != content:
        print(f'  Removed fwd: {fn}')
        content = new_c

# Fix known issues
content = content.replace('static int vbe_mode_active = 0;', 'int vbe_mode_active = 0;', 1)
if 'cmd_date();' in content:
    content = content.replace('cmd_date();', 'cmd_date(NULL);', 1)
if 'cmd_basename(arg);' in content:
    content = content.replace('cmd_basename(arg);', 'cmd_basename(arg, NULL);', 1)

# Fix all calls to migrated funcs with extra args
migrated = set(bodies.keys())
for fn in sorted(migrated):
    pat = re.compile(r'\b' + fn + r'\(([^)]+)\);')
    for m in list(pat.finditer(content)):
        args = m.group(1)
        if ',' in args:
            first = args.split(',')[0].strip()
            ln = content[:m.start()].count('\n') + 1
            content = content[:m.start()] + fn + '(' + first + ');' + content[m.end():]

# Add includes
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

# Count remaining
remaining = len(re.findall(r'static\s+void\s+(cmd_\w+)\s*\([^)]*\)\s*\{', content))
print(f'Remaining cmd_* functions: {remaining}')

# Compile modules
for bn in sorted(written):
    print(f'Compiling {bn}...')
    r = gcc_compile(f'kernel/{bn}', f'build/kernel/{bn}.o')
    if r.returncode == 0:
        print(f'  OK')
    else:
        for line in (r.stderr + r.stdout).split('\n'):
            if 'error:' in line:
                print(f'  ERR: {line[:200]}')

# Compile shell.o
print('Compiling shell.o...')
r = gcc_compile('kernel/shell.c', 'build/kernel/shell.o')
if r.returncode == 0:
    print('shell.o: OK')
else:
    errs = [l[:200] for l in (r.stderr + r.stdout).split('\n') if 'error:' in l]
    for e in errs[:30]:
        print(f'  ERR: {e}')
    print(f'Total errors: {len(errs)}')

print('Done')
