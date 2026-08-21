#!/usr/bin/env python3
"""Fix remaining issues in shell.c"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

fixed = 0
for i, line in enumerate(lines):
    # 1. In app_notepad_main, cmd_edit(arg) -> cmd_edit(file)
    if i >= 16200 and i <= 16300 and 'cmd_edit(arg)' in line:
        lines[i] = line.replace('cmd_edit(arg)', 'cmd_edit(file)')
        print(f'  L{i+1}: cmd_edit(arg) -> cmd_edit(file)')
        fixed += 1

    # 2. In app_registry, games use cmd_ prefix but our stubs don't
    # Remove cmd_ prefix in registry: {"life", ..., cmd_life} -> {"life", ..., life}
    if '{"life"' in line and 'cmd_life' in line:
        lines[i] = line.replace('cmd_life', 'life')
        print(f'  L{i+1}: cmd_life -> life')
        fixed += 1
    if '{"sokoban"' in line and 'cmd_sokoban' in line:
        lines[i] = line.replace('cmd_sokoban', 'sokoban')
        print(f'  L{i+1}: cmd_sokoban -> sokoban')
        fixed += 1
    if '{"typing"' in line and 'cmd_typing' in line:
        lines[i] = line.replace('cmd_typing', 'typing')
        print(f'  L{i+1}: cmd_typing -> typing')
        fixed += 1
    if '{"ascii"' in line and 'cmd_ascii' in line:
        lines[i] = line.replace('cmd_ascii', 'ascii')
        print(f'  L{i+1}: cmd_ascii -> ascii')
        fixed += 1
    if '{"nano"' in line and 'cmd_nano' in line:
        lines[i] = line.replace('cmd_nano', 'nano')
        print(f'  L{i+1}: cmd_nano -> nano')
        fixed += 1
    if '{"edit"' in line and 'cmd_nano' in line:
        lines[i] = line.replace('cmd_nano', 'nano')
        print(f'  L{i+1}: edit -> nano')
        fixed += 1

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)
print(f'Fixed {fixed} lines')
