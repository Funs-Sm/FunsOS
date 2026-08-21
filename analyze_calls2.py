#!/usr/bin/env python3
"""分析所有 cmd_* 调用，找出签名不匹配的问题"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

lines = content.split('\n')

# Find all cmd_* calls and their line numbers
call_re = re.compile(r'\s*(cmd_\w+)\s*\(\s*([^)]*)\s*\)\s*;')

problems = []
for i, line in enumerate(lines):
    m = call_re.search(line)
    if m:
        fn = m.group(1)
        args = m.group(2).strip()
        # Check for games that have different signature
        if fn in ('cmd_life', 'cmd_sokoban', 'cmd_typing', 'cmd_ascii', 'cmd_nano'):
            continue  # These are games, not shell commands
        problems.append((i+1, fn, args, line.strip()))

print(f'Found {len(problems)} cmd_* call sites:')
for ln, fn, args, line in problems:
    # Categorize
    if args == '':
        print(f'  L{ln}: {fn}() - NO ARGS')
    elif ',' in args:
        print(f'  L{ln}: {fn}({args[:40]}...) - MULTI ARGS')
    else:
        print(f'  L{ln}: {fn}({args})')
