#!/usr/bin/env python3
"""Add all missing cmd_* declarations to cmd_all.h"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Get all cmd_* calls
call_pat = re.compile(r'\b(cmd_\w+)\s*\(')
all_calls = set(m.group(1) for m in call_pat.finditer(content))

# Read existing cmd_all.h
with open('kernel/cmd_all.h', 'r') as f:
    header = f.read()

# Find already declared
declared = set()
for m in re.finditer(r'\b(cmd_\w+)\b', header):
    if m.group(1).startswith('cmd_'):
        declared.add(m.group(1))

# Filter out shell_print, shell_last_exit_code, vbe_mode_active
declared -= {'shell_print', 'shell_last_exit_code', 'vbe_mode_active'}

missing = all_calls - declared
print(f'Missing declarations: {len(missing)}')
for fn in sorted(missing):
    print(f'  {fn}')

# Add missing ones before the shell.c definitions section
new_decls = '\n'.join([f'extern void {fn}(const char *arg);' for fn in sorted(missing)])
header = header.replace('/* shell.c definitions */', new_decls + '\n\n/* shell.c definitions */')

with open('kernel/cmd_all.h', 'w') as f:
    f.write(header)

print(f'Updated cmd_all.h')