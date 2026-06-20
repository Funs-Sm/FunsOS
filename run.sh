#!/bin/bash
echo "=========================================="
echo "  Funs Core v0.5 - Quick Run Script"
echo "=========================================="
echo

# Check if QEMU is available
if ! command -v qemu-system-i386 &> /dev/null; then
    echo "[ERROR] qemu-system-i386 not found"
    echo "Install with: sudo apt install qemu-system-x86"
    exit 1
fi

# Build if needed
if [ ! -f build/kernel.elf ]; then
    echo "[INFO] Kernel not built, building now..."
    make all
    if [ $? -ne 0 ]; then
        echo "[ERROR] Build failed!"
        exit 1
    fi
fi

# Build disk image if needed
if [ ! -f build/os.img ]; then
    echo "[INFO] Disk image not found, building..."
    make all
fi

echo "[INFO] Starting Funs Core in QEMU..."
echo "[INFO] Press Ctrl+A then X to exit QEMU"
echo

qemu-system-i386 -drive format=raw,file=build/os.img,if=ide,index=0 -m 128 -serial stdio -no-reboot -no-shutdown 2>&1

echo
echo "[INFO] QEMU exited."
