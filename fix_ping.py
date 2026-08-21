#!/usr/bin/env python3
"""Fix specific call sites in shell.c"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

# Find and fix the cmd_ping() call
fixed = 0
for i, line in enumerate(lines):
    if 'cmd_ping();' in line:
        lines[i] = line.replace('cmd_ping();', 'cmd_ping(arg);')
        print(f'L{i+1}: cmd_ping() -> cmd_ping(arg)')
        fixed += 1

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)
print(f'Fixed {fixed}')