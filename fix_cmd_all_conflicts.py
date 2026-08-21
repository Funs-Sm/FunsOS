#!/usr/bin/env python3
"""Fix cmd_all.c stubs to match cmd_all.h declarations exactly"""
import re

# Read cmd_all.h to get exact declarations
with open('kernel/cmd_all.h', 'r') as f:
    header = f.read()

# Parse all extern declarations
declarations = {}  # name -> (return_type, args_string)
for m in re.finditer(r'extern\s+(\w+(?:\s*\*)?)\s+(cmd_\w+)\s*\(([^)]*)\)\s*;', header):
    ret = m.group(1).strip()
    fn = m.group(2)
    args = m.group(3).strip()
    declarations[fn] = (ret, args)

print(f"Found {len(declarations)} declarations in header")

# Read cmd_all.c
with open('kernel/cmd_all.c', 'r') as f:
    content = f.read()

# Find all function definitions in cmd_all.c
# Match patterns like: void cmd_xxx(const char *arg) { ... } or int cmd_yyy(int argc, char *argv[]) { ... }
defs = {}
for m in re.finditer(r'^(int|void)\s+(cmd_\w+)\s*\(([^)]*)\)\s*\{', content, re.MULTILINE):
    ret = m.group(1)
    fn = m.group(2)
    args = m.group(3).strip()
    defs[fn] = (ret, args, m.start(), m.end())

print(f"Found {len(defs)} definitions in cmd_all.c")

# For each definition, check if it matches declaration
conflicts = []
for fn, (def_ret, def_args, start, end) in defs.items():
    if fn not in declarations:
        # Definition for non-declared function, skip
        continue
    decl_ret, decl_args = declarations[fn]
    if def_ret != decl_ret or def_args != decl_args:
        conflicts.append((fn, def_ret, def_args, decl_ret, decl_args))

print(f"Found {len(conflicts)} conflicts to fix")

# Replace each conflicting definition with one that matches the declaration
lines = content.split('\n')
new_lines = []
i = 0
skip_until_brace_end = False
brace_depth = 0

for j, line in enumerate(lines):
    # Check if this line starts a function definition
    m = re.match(r'^(int|void)\s+(cmd_\w+)\s*\(([^)]*)\)\s*\{(.*)$', line)
    if m:
        ret = m.group(1)
        fn = m.group(2)
        args = m.group(3).strip()
        rest = m.group(4)
        if fn in declarations:
            decl_ret, decl_args = declarations[fn]
            if (ret, args) != (decl_ret, decl_args):
                # Conflict! Replace with correct signature
                new_args_str = decl_args if decl_args else 'void'
                # Replace with stub matching declaration
                if decl_ret == 'int':
                    # Game function
                    new_line = f'int {fn}({new_args_str}) {{ (void)argc; (void)argv; shell_print("{fn}: stub\\n"); return 0; }}'
                    if 'argc' not in decl_args:
                        # Different arg names but same count
                        if ',' in decl_args:
                            # Multiple args - cast each
                            new_line = f'int {fn}({new_args_str}) {{ shell_print("{fn}: stub\\n"); return 0; }}'
                else:
                    new_line = f'void {fn}({new_args_str}) {{ shell_print("{fn}: stub\\n"); shell_last_exit_code = 0; }}'
                new_lines.append(new_line)
                continue
    new_lines.append(line)

new_content = '\n'.join(new_lines)

with open('kernel/cmd_all.c', 'w') as f:
    f.write(new_content)

print("Updated cmd_all.c")
