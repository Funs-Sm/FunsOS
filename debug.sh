#!/bin/bash
echo "=========================================="
echo "  Funs Core v0.5 - Debug Script"
echo "=========================================="
echo

if ! command -v qemu-system-i386 &> /dev/null; then
    echo "[ERROR] qemu-system-i386 not found"
    exit 1
fi

if [ ! -f build/kernel.elf ]; then
    echo "[INFO] Building kernel..."
    make all
fi

echo "[INFO] Starting QEMU with GDB stub on port 1234..."
echo "[INFO] Connect GDB with: target remote localhost:1234"
echo

qemu-system-i386 -drive format=raw,file=build/os.img -m 128 -s -S -serial stdio &
QEMU_PID=$!

echo "[INFO] QEMU PID: $QEMU_PID"
echo "[INFO] Press Ctrl+C to stop..."

trap "kill $QEMU_PID 2>/dev/null; exit 0" INT TERM
wait $QEMU_PID
