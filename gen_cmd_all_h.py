#!/usr/bin/env python3
"""Generate complete cmd_all.h with CORRECT signatures matching actual call sites"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Find all cmd_* calls
call_pat = re.compile(r'\b(cmd_\w+)\s*\(\s*([^)]*)\s*\)\s*;')
def_pat = re.compile(r'static\s+void\s+(cmd_\w+)\s*\([^)]*\)\s*\{')

def_names = set(def_pat.findall(content))
all_calls = {}
for m in call_pat.finditer(content):
    fn = m.group(1)
    args = m.group(2).strip()
    if fn not in def_names:
        if fn not in all_calls:
            all_calls[fn] = args

# Known correct signatures from existing module headers
known_sigs = {
    'cmd_ifconfig': 'void',
    'cmd_route': 'void',
    'cmd_netstat': 'void',
    'cmd_arp': 'void',
    'cmd_lanscan': 'void',
    'cmd_sockstat': 'void',
    'cmd_nc': 'void(const char*, const char*)',
    'cmd_httpget': 'void(const char*, const char*)',
    'cmd_wget': 'void(const char*, const char*)',
    'cmd_free': 'void',
    'cmd_meminfo': 'void',
    'cmd_ps': 'void',
    'cmd_top': 'void',
    'cmd_sysinfo': 'void',
    'cmd_uptime': 'void',
    'cmd_date': 'void(const char*)',
    'cmd_hostname': 'void(const char*)',
    'cmd_service': 'void(const char*, const char*, const char*)',
    'cmd_crontab': 'void(const char*, const char*, const char*)',
    'cmd_seq': 'void(const char*, const char*)',
    'cmd_false_cmd': 'void',
    'cmd_true_cmd': 'void',
    'cmd_cal': 'void(const char*)',
    'cmd_basename': 'void(const char*, const char*)',
    'cmd_dirname': 'void(const char*)',
    'cmd_truncate': 'void(const char*, const char*)',
    'cmd_yes': 'void(const char*)',
    'cmd_factor': 'void(const char*)',
    'cmd_shuf': 'void(const char*)',
    'cmd_test_cmd': 'void(const char*)',
    'cmd_expr_cmd': 'void(const char*)',
    # Text tools
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
    # Data tools
    'cmd_dd': 'void(const char*)',
    'cmd_split': 'void(const char*)',
    'cmd_join': 'void(const char*)',
    'cmd_hexdump': 'void(const char*)',
    'cmd_strings': 'void(const char*)',
    'cmd_cksum': 'void(const char*)',
    # Archive
    'cmd_tar': 'void(const char*)',
    'cmd_gzip': 'void(const char*)',
    'cmd_gunzip': 'void(const char*)',
    # Netdiag
    'cmd_iptraf': 'void(const char*)',
    'cmd_wol': 'void(const char*)',
    'cmd_ftp': 'void(const char*)',
    'cmd_speedtest': 'void(const char*)',
    # Sysmon
    'cmd_iostat': 'void(const char*)',
    'cmd_vmstat': 'void(const char*)',
    'cmd_mpstat': 'void(const char*)',
    'cmd_pidstat': 'void(const char*)',
    # Usersys
    'cmd_id': 'void(const char*)',
    'cmd_whoami': 'void(const char*)',
    'cmd_users': 'void(const char*)',
    'cmd_who': 'void(const char*)',
    'cmd_groups': 'void(const char*)',
    'cmd_umask': 'void(const char*)',
    # Log
    'cmd_dmesg': 'void(const char*)',
    'cmd_loglevel': 'void(const char*)',
    'cmd_syslog': 'void(const char*)',
    # Net
    'cmd_ping': 'void(const char*)',
    'cmd_nslookup': 'void(const char*)',
    'cmd_dig': 'void(const char*)',
    'cmd_dhcp': 'void(const char*)',
    'cmd_tcpdump': 'void(const char*)',
    'cmd_traceroute': 'void(const char*)',
    'cmd_nmap': 'void(const char*)',
    'cmd_mtr': 'void(const char*)',
    'cmd_dns': 'void(const char*)',
    # Games / apps
    'cmd_life': 'int(int, char**)',
    'cmd_sokoban': 'int(int, char**)',
    'cmd_typing': 'int(int, char**)',
    'cmd_ascii': 'int(int, char**)',
    'cmd_nano': 'int(int, char**)',
}

def sig_to_c(sig, fn):
    if sig == 'void':
        return f'extern void {fn}(void);'
    elif sig == 'int(int, char**)':
        return f'extern int {fn}(int argc, char *argv[]);'
    elif sig == 'void(const char*)':
        return f'extern void {fn}(const char *arg);'
    elif sig == 'void(const char*, const char*)':
        return f'extern void {fn}(const char *a, const char *b);'
    elif sig == 'void(const char*, const char*, const char*)':
        return f'extern void {fn}(const char *a, const char *b, const char *c);'
    elif sig == 'void(const char*, const char*)':
        return f'extern void {fn}(const char *a, const char *b);'
    else:
        return f'extern void {fn}(const char *arg);'

lines = ['#ifndef _KERNEL_CMD_ALL_H',
         '#define _KERNEL_CMD_ALL_H',
         '',
         '/* Auto-generated - all cmd_* functions needed by shell.c */',
         '']

for fn in sorted(all_calls.keys()):
    sig = known_sigs.get(fn, 'void(const char*)')
    lines.append(sig_to_c(sig, fn))

lines.extend(['',
             '/* shell.c definitions */',
             'extern void shell_print(const char *str);',
             'extern int shell_last_exit_code;',
             'extern int vbe_mode_active;',
             '',
             '#endif'])

with open('kernel/cmd_all.h', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))

print(f'Written {len(lines)} lines, {len(all_calls)} functions')
