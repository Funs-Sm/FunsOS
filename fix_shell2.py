#!/usr/bin/env python3
"""Fix remaining issues in shell.c"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

print(f'Before: {len(content)} chars')

# 1. Remove broken game function bodies (int cmd_life(arg char *argv[]) style)
for fn in ['cmd_life', 'cmd_sokoban', 'cmd_typing', 'cmd_ascii', 'cmd_nano']:
    # Pattern: "static int cmd_xxx(arg char *argv[]) {\n...\n}\n"
    pat = re.compile(r'\n\s*static\s+int\s+' + re.escape(fn) + r'\(arg\s+char\s+\*argv\[\]\)\s*\{', re.DOTALL)
    m = pat.search(content)
    if m:
        # Find matching brace
        bs = m.end() - 1
        depth = 1; i = bs + 1
        while i < len(content) and depth > 0:
            if content[i] == '{': depth += 1
            elif content[i] == '}': depth -= 1
            i += 1
        body = content[m.start():i]
        print(f'  Removing {fn} body: {len(body)}B')
        content = content[:m.start()] + content[i:]

# 2. Fix "cmd_grep(arcmd_grep(arg)" pattern
content = re.sub(r'cmd_grep\(arcmd_grep\(arg\)\)', 'cmd_grep(arg)', content)

# 3. Fix "cmd_ln(arcmd_ln(arg)" pattern
content = re.sub(r'cmd_ln\(arcmd_ln\(arg\)\)', 'cmd_ln(arg)', content)

# 4. Fix "cmd_httpget(arg arg2)" pattern
content = re.sub(r'cmd_httpget\(arg\s+arg2\)', 'cmd_httpget(arg)', content)

# 5. Fix "cmd_nc(arg arg2)" pattern
content = re.sub(r'cmd_nc\(arg\s+arg2\)', 'cmd_nc(arg)', content)

# 6. Fix "cmd_crontab(arg arg3)" pattern
content = re.sub(r'cmd_crontab\(arg\s+arg3\)', 'cmd_crontab(arg)', content)

# 7. Fix "cmd_service(arg arg3)" pattern
content = re.sub(r'cmd_service\(arg\s+arg3\)', 'cmd_service(arg)', content)

# 8. Fix cmd_dd_full declaration issue - it's in shell.c body, not a call
# Let's check what cmd_dd_full looks like in the file
# (it's called from within other functions, not as a dispatch call)

# 9. Check remaining cmd_* calls for issues
lines = content.split('\n')
call_re = re.compile(r'\s*(cmd_\w+)\s*\(\s*([^)]*)\s*\)\s*;')
issues = []
for i, line in enumerate(lines):
    m = call_re.search(line)
    if m:
        fn = m.group(1)
        args = m.group(2).strip()
        if args == '' or ',' in args:
            issues.append((i+1, fn, args))

if issues:
    print(f'\nRemaining issues ({len(issues)}):')
    for ln, fn, args in issues[:30]:
        print(f'  L{ln}: {fn}({args})')
else:
    print('\nAll call sites look correct!')

# Write fixed shell.c
with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(content)
print(f'\nWritten: {len(content)} chars')
