@echo off
echo ==========================================
echo   Funs Core v0.7 - Quick Run Script
echo ==========================================
echo.

REM Check if QEMU is available
where qemu-system-i386 >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] qemu-system-i386 not found in PATH
    echo Please install QEMU: https://www.qemu.org/download/
    echo Or add QEMU to your PATH
    pause
    exit /b 1
)

REM Build if needed
if not exist build\kernel.elf (
    echo [INFO] Kernel not built, building now...
    mingw32-make all
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Build failed!
        pause
        exit /b 1
    )
)

REM Build disk image if needed
if not exist build\os.img (
    echo [INFO] Disk image not found, building...
    mingw32-make all
)

echo [INFO] Starting Funs Core in QEMU...
echo [INFO] Press Ctrl+A then X to exit QEMU
echo.

REM Run with QEMU - no-reboot to catch triple faults
REM -no-acpi: disable ACPI/IOAPIC so interrupts route through PIC (keyboard+timer need this)
REM -monitor none: prevent QEMU monitor from stealing keystrokes
REM -serial file: write serial output to file (not stealing keyboard focus)
qemu-system-i386 -drive format=raw,file=build\os.img,if=ide,index=0 -m 128 -monitor none -serial file:serial.log -no-reboot -no-shutdown 2>&1

echo.
echo [INFO] QEMU exited. Serial log saved to serial.log
pause
