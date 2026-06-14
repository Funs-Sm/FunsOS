#!/usr/bin/env python3
"""Assemble the flat raw disk image used by the kernel.

Layout (sector == 512 bytes):
    sector 0         : boot.bin   (MBR, 512 bytes)
    sector 1         : reserved (one sector of zeros, MBRLDR alignment)
    sectors 2..21    : loader.bin (20 sectors, loaded by boot.asm
                       from LBA 2 for 20 sectors, to 0x0000:0x1000)
    sectors 22..25   : stage2.bin (4 sectors, 32-bit PM 2nd-stage
                       loader, loaded by loader.asm to 0x80000)
    sectors 26+      : kernel.bin (the kernel, linked at 0x100000)

The script is intentionally simple and cross-platform because the
build is run on Windows (mingw32-make + python 3).
"""

import os
import sys
import struct

SECTOR = 512
BOOT_LBA   = 0
LOADER_LBA = 2
LOADER_SECTORS = 20
STAGE2_LBA = LOADER_LBA + LOADER_SECTORS  # 22
STAGE2_SECTORS = 4
KERNEL_LBA = STAGE2_LBA + STAGE2_SECTORS  # 26

def fatal(msg):
    sys.stderr.write("mkimg: %s\n" % msg)
    sys.exit(1)

def pad_to(data, size, label):
    if len(data) > size:
        fatal("%s is %d bytes, exceeds %d" % (label, len(data), size))
    return data + b"\x00" * (size - len(data))

def main():
    if len(sys.argv) != 6:
        fatal("usage: mkimg.py <out.img> <boot.bin> <loader.bin> <stage2.bin> <kernel.bin>")

    out_path = sys.argv[1]
    boot_path = sys.argv[2]
    loader_path = sys.argv[3]
    stage2_path = sys.argv[4]
    kernel_path = sys.argv[5]

    with open(boot_path, "rb") as f:
        boot = f.read()
    with open(loader_path, "rb") as f:
        loader = f.read()
    with open(stage2_path, "rb") as f:
        stage2 = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    if len(boot) != SECTOR:
        fatal("boot.bin must be exactly 512 bytes, got %d" % len(boot))

    boot_padded   = pad_to(boot, SECTOR, "boot.bin")
    loader_padded = pad_to(loader, LOADER_SECTORS * SECTOR, "loader.bin")
    stage2_padded = pad_to(stage2, STAGE2_SECTORS * SECTOR, "stage2.bin")

    with open(out_path, "wb") as f:
        f.write(boot_padded)                       # sector 0
        f.write(b"\x00" * SECTOR)                 # sector 1 (reserved)
        f.write(loader_padded)                    # sectors 2..21
        f.write(stage2_padded)                    # sectors 22..25
        f.write(kernel)                           # sector 26+

    size = SECTOR * 2 + len(loader_padded) + len(stage2_padded) + len(kernel)
    sys.stdout.write("mkimg: wrote %s (%d bytes, %d sectors)\n"
                     % (out_path, size, (size + SECTOR - 1) // SECTOR))

if __name__ == "__main__":
    main()
