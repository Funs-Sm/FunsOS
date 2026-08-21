#!/usr/bin/env python3
"""分析 shell.c 当前状态"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Find all static void cmd_* function bodies
body_pat = re.compile(r'static\s+void\s+(cmd_\w+)\s*\([^)]*\)\s*\{')
body_names = set(m.group(1) for m in body_pat.finditer(content))

# Find all cmd_* call sites (excluding definitions)
call_pat = re.compile(r'\b(cmd_\w+)\s*\(')
call_names = set(m.group(1) for m in call_pat.finditer(content))

# Calls that are NOT definitions
call_only = call_names - body_names

print(f'Function bodies in shell.c: {len(body_names)}')
print(f'Unique call sites (excluding bodies): {len(call_only)}')

# Find calls in switch/case table
print('\nFunction bodies:')
for n in sorted(body_names):
    print(f'  {n}')

print('\nCalls NOT defined as body in shell.c:')
for n in sorted(call_only):
    print(f'  {n}')

# Check if shell_print is available
if 'shell_print' in content:
    print('\nshell_print found in shell.c: YES')
else:
    print('\nshell_print found in shell.c: NO')

# Check shell_last_exit_code
if 'shell_last_exit_code' in content:
    print('shell_last_exit_code found: YES')
if 'int shell_last_exit_code' in content:
    print('int shell_last_exit_code found: YES')
if 'extern int shell_last_exit_code' in content:
    print('extern int shell_last_exit_code found: YES')
if 'static int last_exit_code' in content:
    print('STATIC last_exit_code found: YES')
