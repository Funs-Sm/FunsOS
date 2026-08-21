#!/usr/bin/env python3
"""分析 shell.c 中的 cmd_* 调用上下文"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Find the command dispatch table
# Look for patterns like: { "name", cmd_name } or "name": cmd_name
# Also look for switch statements

# Find all lines with cmd_* calls
lines = content.split('\n')
call_lines = []
for i, line in enumerate(lines):
    if re.search(r'\bcmd_\w+\s*\(', line):
        # Get context: 5 lines before and after
        start = max(0, i-3)
        end = min(len(lines), i+4)
        ctx = lines[start:end]
        call_lines.append((i+1, ctx, line.strip()))

print(f'Call sites: {len(call_lines)}')

# Show first 50
for ln, ctx, line in call_lines[:50]:
    print(f'\n--- Line {ln} ---')
    for j, l in enumerate(ctx):
        marker = '>>>' if j == 3 else '   '
        actual_ln = i - 3 + j + 1
        print(f'{marker} {actual_ln}: {l}')
