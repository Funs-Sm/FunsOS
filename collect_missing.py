#!/usr/bin/env python3
"""Add extern declarations to fix implicit declarations, using void(void) signature
for functions we don't know the signature of."""
import re
import subprocess

# Run a partial compile to get all errors
result = subprocess.run(
    ['cmd', '/c', 'mingw32-make'],
    capture_output=True, text=True, cwd='D:/Software/Project/5'
)

errors = result.stderr
print(f"Got {len(errors)} chars of output")

# Find all "implicit declaration of function 'XXX'" lines
fns = set()
for m in re.finditer(r"implicit declaration of function '(\w+)'", errors):
    fns.add(m.group(1))

# Find "too many arguments" lines
for m in re.finditer(r"too many arguments to function '(\w+)'; expected \d+, have \d+", errors):
    fns.add(m.group(1))

# Find "too few arguments" lines
for m in re.finditer(r"too few arguments to function '(\w+)'", errors):
    fns.add(m.group(1))

print(f"Found {len(fns)} functions to add")

# Filter out standard library functions
STDLIB = {'atoi', 'strncmp', 'snprintf', 'printf', 'sprintf', 'strlen',
          'strcmp', 'strcpy', 'memset', 'memcpy', 'malloc', 'free',
          'fopen', 'fclose', 'fread', 'fwrite', 'fseek', 'ftell',
          'fputc', 'fputs', 'fgets', 'getc', 'putc', 'puts', 'gets',
          'exit', 'abort', 'atoi', 'atol', 'strtol', 'strtoul',
          'va_start', 'va_end', 'va_arg',
          'panic', 'klog_info', 'klog_warn', 'klog_error', 'klog_debug',
          'kbd_poll'}

# Some are likely cmd_* functions but already declared in module headers; check first
# Add declarations to cmd_all.h
with open('kernel/cmd_all.h', 'r') as f:
    header = f.read()

# Get existing declarations in cmd_all.h
existing = set()
for m in re.finditer(r'extern\s+\w+(?:\s*\*)?\s+(\w+)\s*\(', header):
    existing.add(m.group(1))

# Check all module headers too
import glob, os
for mh in glob.glob('kernel/*.h'):
    with open(mh, 'r') as f:
        c = f.read()
    for m in re.finditer(r'extern\s+\w+(?:\s*\*)?\s+(\w+)\s*\(', c):
        existing.add(m.group(1))

# Add shell_print, etc that are already in shell.h
to_add = []
for fn in fns:
    if fn in existing or fn in STDLIB:
        continue
    if fn.startswith('cmd_'):
        # Already should be in cmd_all.h
        if fn in header:
            continue
    # Skip standard C lib
    if fn in {'atoi'}:
        continue
    to_add.append(fn)

print(f"To add declarations: {len(to_add)}")
print(f"First 20: {to_add[:20]}")
