#!/usr/bin/env python3
"""Fix the 2 remaining broken lines in shell.c"""
with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

print(f'Total lines: {len(lines)}')

# Find and fix the two broken lines
fixed = 0
for i, line in enumerate(lines):
    if 'arcmd_grep(arg' in line:
        lines[i] = '            cmd_grep(arg);\n'
        print(f'  Fixed arcmd_grep at line {i+1}')
        fixed += 1
    if 'arcmd_ln(arg' in line:
        lines[i] = '        cmd_ln(arg);\n'
        print(f'  Fixed arcmd_ln at line {i+1}')
        fixed += 1

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)
print(f'Fixed {fixed} lines')
