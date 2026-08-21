#!/usr/bin/env python3
"""Fix app_notepad_main in shell.c"""
with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    # In app_notepad_main context (around line 16249)
    if i >= 16240 and i <= 16300:
        if 'cmd_edit(arg)' in line:
            lines[i] = line.replace('cmd_edit(arg)', 'cmd_edit(file)')
            print(f'  L{i+1}: fixed cmd_edit(arg)')

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)
print('Done')
