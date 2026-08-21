#!/usr/bin/env python3
"""Fix ALL call sites in shell.c to match actual function signatures"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

print(f'Before: {len(content)} chars')

# ===== CORRECT ALL CALLS TO MATCH ACTUAL MODULE SIGNATURES =====

# cmd_net.h
content = re.sub(r'\bcmd_ifconfig\(arg\);', 'cmd_ifconfig();', content)
content = re.sub(r'\bcmd_route\(arg\);', 'cmd_route();', content)
content = re.sub(r'\bcmd_netstat\(arg\);', 'cmd_netstat();', content)
content = re.sub(r'\bcmd_arp\(arg\);', 'cmd_arp();', content)
content = re.sub(r'\bcmd_lanscan\(arg\);', 'cmd_lanscan();', content)
content = re.sub(r'\bcmd_sockstat\(arg\);', 'cmd_sockstat();', content)
content = re.sub(r'\bcmd_httpget\(arg\);', 'cmd_httpget(NULL, NULL);', content)
content = re.sub(r'\bcmd_wget\(arg\);', 'cmd_wget(NULL, NULL);', content)

# cmd_nc takes 2 args
content = re.sub(r'\bcmd_nc\(arg\);', 'cmd_nc(NULL, NULL);', content)

# cmd_mem.h
content = re.sub(r'\bcmd_free\(arg\);', 'cmd_free();', content)
content = re.sub(r'\bcmd_meminfo\(arg\);', 'cmd_meminfo();', content)

# cmd_proc.h
content = re.sub(r'\bcmd_ps\(arg\);', 'cmd_ps();', content)
content = re.sub(r'\bcmd_top\(arg\);', 'cmd_top();', content)

# cmd_sysinfo.h
content = re.sub(r'\bcmd_sysinfo\(arg\);', 'cmd_sysinfo();', content)

# cmd_service.h - 3 args
content = re.sub(r'\bcmd_service\(arg\);', 'cmd_service(NULL, NULL, NULL);', content)

# cmd_crontab.h - 3 args
content = re.sub(r'\bcmd_crontab\(arg\);', 'cmd_crontab(NULL, NULL, NULL);', content)

# cmd_utils.h
content = re.sub(r'\bcmd_seq\(arg\);', 'cmd_seq(NULL, NULL);', content)
content = re.sub(r'\bcmd_false_cmd\(arg\);', 'cmd_false_cmd();', content)
content = re.sub(r'\bcmd_true_cmd\(arg\);', 'cmd_true_cmd();', content)

# cmd_path.h
content = re.sub(r'\bcmd_truncate\(arg\);', 'cmd_truncate(NULL, NULL);', content)

# ===== ALSO: Remove the #include "cmd_all.h" from shell.c =====
# It was inserted at the very beginning
lines = content.split('\n')
if lines[0].strip() == '#include "cmd_all.h"':
    lines = lines[1:]
content = '\n'.join(lines)

# ===== Fix remaining game-related issues =====
# Remove the static game function bodies if any remain
for fn in ['cmd_life', 'cmd_sokoban', 'cmd_typing', 'cmd_ascii', 'cmd_nano']:
    pat = re.compile(r'\n\s*static\s+int\s+' + re.escape(fn) + r'\(.*?\n\s*\}', re.DOTALL)
    m = pat.search(content)
    if m:
        print(f'  Still found {fn} body, removing')
        content = pat.sub('\n', content)

# Write
with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(content)
print(f'After: {len(content)} chars')

# Verify
call_re = re.compile(r'\s*(cmd_\w+)\s*\(\s*([^)]*)\s*\)\s*;')
issues = []
lines = content.split('\n')
for i, line in enumerate(lines):
    m = call_re.search(line)
    if m:
        fn = m.group(1)
        args = m.group(2).strip()
        # These are the known OK ones
        if fn == 'cmd_basename' and ',' in args:
            continue  # OK: cmd_basename(path, suffix)
        if fn == 'cmd_httpget' and ',' in args:
            continue  # OK
        if fn == 'cmd_wget' and ',' in args:
            continue  # OK
        if fn == 'cmd_nc' and ',' in args:
            continue  # OK
        if fn == 'cmd_seq' and ',' in args:
            continue  # OK
        if fn == 'cmd_truncate' and ',' in args:
            continue  # OK
        if fn == 'cmd_service' and ',' in args:
            continue  # OK
        if fn == 'cmd_crontab' and ',' in args:
            continue  # OK
        # Check if it's a dispatch call pattern (if/else chain)
        if args == 'arg' or args == 'NULL':
            continue  # OK - single arg
        if args == '':
            continue  # OK - no args
        issues.append((i+1, fn, args))

if issues:
    print(f'\nRemaining issues ({len(issues)}):')
    for ln, fn, args in issues:
        print(f'  L{ln}: {fn}({args})')
else:
    print('\nAll call sites OK!')
