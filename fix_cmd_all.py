#!/usr/bin/env python3
"""Fix cmd_all.c - replace STUB() with STUB_NAME()"""
import re

with open('kernel/cmd_all.c', 'r', encoding='utf-8') as f:
    content = f.read()

# Replace STUB(cmd_xxx) with STUB_NAME(cmd_xxx, "xxx")
def replace_stub(m):
    fn = m.group(1)
    # Extract name after cmd_ (strip "cmd_" prefix)
    name = fn[4:]  # remove "cmd_"
    return f'STUB_NAME({fn}, "{name}")'

content = re.sub(r'STUB\(cmd_(\w+)\)', replace_stub, content)

# Also replace STUB_NARG -> STUB_NAME for basename
content = content.replace('STUB_NARG(cmd_basename)', 'STUB_NAME(cmd_basename, "basename")')

# Fix the STUB2 macro (it's not used anymore)
content = re.sub(r'\n#define STUB2.*?\n', '\n', content, flags=re.DOTALL)

with open('kernel/cmd_all.c', 'w', encoding='utf-8') as f:
    f.write(content)
print("Fixed!")
