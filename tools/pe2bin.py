#!/usr/bin/env python3
"""
tools/pe2bin.py - convert a PE/COFF i386 kernel.elf into a flat binary
where each section's bytes are placed at the byte offset corresponding to
(VMA - 0x100000), filling any gaps with zeros.

This makes the kernel.bin linear-loadable: the bootloader reads from file
offset 0 and writes to physical address 0x100000 + offset, which exactly
matches the kernel's expected VMA layout.
"""

import struct
import sys

def main():
    if len(sys.argv) != 3:
        print("usage: pe2bin.py <input.elf> <output.bin>", file=sys.stderr)
        return 1

    in_path, out_path = sys.argv[1], sys.argv[2]
    with open(in_path, 'rb') as f:
        data = bytearray(f.read())

    # Parse PE/COFF headers
    # DOS header: e_lfanew at 0x3c -> PE header offset
    if data[:2] != b'MZ':
        print("not a PE file", file=sys.stderr)
        return 1
    pe_off = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe_off:pe_off+4] != b'PE\x00\x00':
        print("bad PE sig", file=sys.stderr)
        return 1

    coff_off = pe_off + 4
    num_sections = struct.unpack_from('<H', data, coff_off + 2)[0]
    opt_hdr_size = struct.unpack_from('<H', data, coff_off + 16)[0]
    opt_off = coff_off + 20

    # Optional header: ImageBase at offset 0x1c (PE32) or 0x10 (PE32+)
    magic = struct.unpack_from('<H', data, opt_off)[0]
    if magic == 0x10b:  # PE32
        image_base = struct.unpack_from('<I', data, opt_off + 0x1c)[0]
    elif magic == 0x20b:  # PE32+
        image_base = struct.unpack_from('<Q', data, opt_off + 0x18)[0]
    else:
        print(f"unknown PE magic {magic:#x}", file=sys.stderr)
        return 1

    section_off = opt_off + opt_hdr_size

    # Read each section, place at VMA offset
    sections = []
    max_vma_size = 0
    for i in range(num_sections):
        so = section_off + i * 40
        name = data[so:so+8].rstrip(b'\x00').decode('ascii', 'replace')
        vsize = struct.unpack_from('<I', data, so+8)[0]
        vaddr = struct.unpack_from('<I', data, so+12)[0]
        raw_size = struct.unpack_from('<I', data, so+16)[0]
        raw_ptr = struct.unpack_from('<I', data, so+20)[0]
        sections.append((name, vaddr, vsize, raw_size, raw_ptr))
        end = vaddr + vsize
        if end > max_vma_size:
            max_vma_size = end

    if image_base != 0x100000:
        print(f"warning: ImageBase={image_base:#x}, expected 0x100000", file=sys.stderr)

    # Allocate output - size = max RVA + max(VSize, RawSize)
    out_size = max_vma_size
    out = bytearray(b'\x00' * out_size)

    for (name, vaddr, vsize, raw_size, raw_ptr) in sections:
        if vaddr + vsize > len(out):
            out.extend(b'\x00' * (vaddr + vsize - len(out)))
        size = min(raw_size, vsize)
        if raw_ptr and size:
            out[vaddr:vaddr+size] = data[raw_ptr:raw_ptr+size]

    with open(out_path, 'wb') as f:
        f.write(out)

    print(f"{in_path} -> {out_path}: {len(out)} bytes "
          f"({num_sections} sections, ImageBase={image_base:#x})")
    return 0


if __name__ == '__main__':
    sys.exit(main())
