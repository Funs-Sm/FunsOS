#!/usr/bin/env python3
"""Remove conflicting declarations from cmd_all.h - the ones that have different
signatures in module headers. For these, the module header declaration wins."""

# Functions whose signatures are owned by other module headers
# (cmd_all.h should NOT redeclare them with different signatures)
conflict_fns = [
    'cmd_basename', 'cmd_truncate',  # cmd_path.h
    'cmd_uptime',  # cmd_sysinfo2.h
    'cmd_seq', 'cmd_false_cmd', 'cmd_true_cmd',  # cmd_utils.h
    'cmd_cal', 'cmd_yes', 'cmd_factor', 'cmd_shuf',
    'cmd_test_cmd', 'cmd_expr_cmd',  # cmd_utils.h
    'cmd_ifconfig', 'cmd_route', 'cmd_arp', 'cmd_netstat',  # cmd_net.h
    'cmd_lanscan', 'cmd_sockstat', 'cmd_nc', 'cmd_ping',
    'cmd_nslookup', 'cmd_dig', 'cmd_dhcp', 'cmd_tcpdump',
    'cmd_traceroute', 'cmd_nmap', 'cmd_mtr', 'cmd_dns',
    'cmd_fw', 'cmd_ss', 'cmd_httpget', 'cmd_wget',
    'cmd_free', 'cmd_meminfo',  # cmd_mem.h
    'cmd_ps', 'cmd_top',  # cmd_proc.h
    'cmd_sysinfo',  # cmd_sysinfo.h
    'cmd_service', 'cmd_crontab',  # cmd_service.h, cmd_crontab.h
    'cmd_uname', 'cmd_date', 'cmd_hostname',  # cmd_sysinfo2.h
    'cmd_dmesg', 'cmd_loglevel', 'cmd_syslog',  # cmd_log.h
    'cmd_cut', 'cmd_paste', 'cmd_tr', 'cmd_rev', 'cmd_fold',  # cmd_text.h
    'cmd_expand', 'cmd_unexpand', 'cmd_nl', 'cmd_look',
    'cmd_comm', 'cmd_tsort',
    'cmd_dd', 'cmd_split', 'cmd_join', 'cmd_hexdump',  # cmd_data.h
    'cmd_strings', 'cmd_cksum',
    'cmd_tar', 'cmd_gzip', 'cmd_gunzip',  # cmd_archive.h
    'cmd_iptraf', 'cmd_wol', 'cmd_ftp', 'cmd_speedtest',  # cmd_netdiag.h
    'cmd_iostat', 'cmd_vmstat', 'cmd_mpstat', 'cmd_pidstat',  # cmd_sysmon.h
    'cmd_id', 'cmd_whoami', 'cmd_users', 'cmd_who',  # cmd_usersys.h
    'cmd_groups', 'cmd_umask',
    'cmd_dirname', 'cmd_realpath',  # cmd_path.h
    'cmd_ipcs', 'cmd_slabtop', 'cmd_sync', 'cmd_pwd', 'cmd_df',
    'cmd_netmon', 'cmd_sigstat',
]

with open('kernel/cmd_all.h', 'r') as f:
    content = f.read()

import re
for fn in conflict_fns:
    # Remove all extern declarations for this function
    pat = re.compile(r'extern void ' + re.escape(fn) + r'\([^)]*\);\n')
    content = pat.sub('', content)
    pat = re.compile(r'extern int ' + re.escape(fn) + r'\([^)]*\);\n')
    content = pat.sub('', content)

with open('kernel/cmd_all.h', 'w') as f:
    f.write(content)

print(f'Removed {len(conflict_fns)} conflict declarations')