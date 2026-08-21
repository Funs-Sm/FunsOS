#!/usr/bin/env python3
"""Fix the 7 remaining issues in shell.c"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# 1. cmd_edit(file) and cmd_edit("untitled.txt") - need to be cmd_edit(arg)
content = re.sub(r'cmd_edit\("untitled\.txt"\)', 'cmd_edit(arg)', content)
content = re.sub(r'cmd_edit\(file\)', 'cmd_edit(arg)', content)

# 2. cmd_time_cmd(full_cmd) -> cmd_time_cmd(arg)
content = re.sub(r'cmd_time_cmd\(full_cmd\)', 'cmd_time_cmd(arg)', content)

# 3. cmd_grep(arg2) -> cmd_grep(arg)
content = re.sub(r'cmd_grep\(arg2\)', 'cmd_grep(arg)', content)

# 4. cmd_grep(arcmd_grep(arg) -> cmd_grep(arg)
content = re.sub(r'cmd_grep\(arcmd_grep\(arg\)\)', 'cmd_grep(arg)', content)

# 5. cmd_ln(arcmd_ln(arg) -> cmd_ln(arg)
content = re.sub(r'cmd_ln\(arcmd_ln\(arg\)\)', 'cmd_ln(arg)', content)

# 6. cmd_sudo(arg ? full_cmd : NULL) -> cmd_sudo(arg)
content = re.sub(r'cmd_sudo\(arg \? full_cmd : NULL\)', 'cmd_sudo(arg)', content)

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(content)
print("Fixed!")

# Verify
call_re = re.compile(r'\s*(cmd_\w+)\s*\(\s*([^)]*)\s*\)\s*;')
lines = content.split('\n')
issues = []
for i, line in enumerate(lines):
    m = call_re.search(line)
    if m:
        fn = m.group(1)
        args = m.group(2).strip()
        if fn == 'cmd_basename' and ',' in args: continue
        if fn in ('cmd_httpget','cmd_wget','cmd_nc','cmd_seq','cmd_truncate','cmd_service','cmd_crontab') and ',' in args: continue
        if args == 'arg' or args == 'NULL' or args == '': continue
        issues.append((i+1, fn, args))

if issues:
    print(f'Remaining issues: {issues}')
else:
    print('All call sites OK!')
