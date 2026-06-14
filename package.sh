#!/bin/bash
echo "=========================================="
echo "  Funs Core v0.5 - Package Script"
echo "=========================================="
echo

# Build everything
echo "[1/4] Building kernel..."
make all
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed!"
    exit 1
fi

# Create package directory
PKG_DIR="funs-core-0.5"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/boot"
mkdir -p "$PKG_DIR/kernel"

# Copy files
echo "[2/4] Copying files..."
cp build/os.img "$PKG_DIR/boot/"
cp build/kernel.elf "$PKG_DIR/kernel/"
cp boot/linker.ld "$PKG_DIR/kernel/"
cp Makefile "$PKG_DIR/"
cp run.sh "$PKG_DIR/"
cp debug.sh "$PKG_DIR/"
chmod +x "$PKG_DIR/run.sh" "$PKG_DIR/debug.sh"

# Create README
echo "[3/4] Creating README..."
cat > "$PKG_DIR/README.txt" << 'READMEEOF'
Funs Core v0.5
================

A 32-bit x86 operating system kernel.

Quick Start:
  1. Install QEMU: sudo apt install qemu-system-x86
  2. Run: ./run.sh
  3. For debugging: ./debug.sh

Requirements:
  - QEMU (qemu-system-i386)
  - 128MB+ RAM for QEMU

Build from source:
  - make (GNU Make)
  - GCC with 32-bit support (gcc-multilib)
  - NASM assembler
READMEEOF

# Create tarball
echo "[4/4] Creating archive..."
tar -czf "$PKG_DIR.tar.gz" "$PKG_DIR"
echo "[OK] Created $PKG_DIR.tar.gz"

echo
echo "[DONE] Package complete!"
