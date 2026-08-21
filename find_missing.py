#!/usr/bin/env python3
"""Extract all missing cmd_* declarations needed by shell.c"""
import re

with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Find all cmd_* calls in shell.c (excluding definitions)
call_pat = re.compile(r'\b(cmd_\w+)\s*\(')

# Find all cmd_* definitions (static void cmd_xxx)
def_pat = re.compile(r'static\s+void\s+(cmd_\w+)\s*\([^)]*\)\s*\{')

def_names = set(def_pat.findall(content))

# All cmd_* calls
call_names = []
for m in call_pat.finditer(content):
    fn = m.group(1)
    if fn not in def_names:  # not defined locally
        if fn not in call_names:
            call_names.append(fn)

print(f'Functions called but not defined locally: {len(call_names)}')
for fn in sorted(call_names):
    print(f'  {fn}')
