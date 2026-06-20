@echo off
echo ==========================================
echo   Funs Core v0.6 - Debug Script
echo ==========================================
echo.

where qemu-system-i386 >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] qemu-system-i386 not found in PATH
    pause
    exit /b 1
)

if not exist build\kernel.elf (
    echo [INFO] Building kernel with debug symbols...
    mingw32-make all
)

echo [INFO] Starting QEMU with GDB stub on port 1234...
echo [INFO] Connect GDB with: target remote localhost:1234
echo [INFO] In another terminal run: gdb build/kernel.elf
echo.

start /b qemu-system-i386 -drive format=raw,file=build\os.img,if=ide,index=0 -m 128 -s -S -serial stdio 2>&1

echo [INFO] QEMU waiting for GDB connection on port 1234...
echo [INFO] Press any key to stop QEMU...
pause >nul
taskkill /f /im qemu-system-i386.exe >nul 2>&1
