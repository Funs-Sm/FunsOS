@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo  Funs Core v0.7 - Automated Test Script
echo ==========================================
echo.
echo [TEST] Starting automated kernel test...
echo [TEST] This will inject keystrokes automatically
echo [TEST] Output captured in test_output.log
echo.

REM Clean old logs
if exist test_output.log del test_output.log
if exist serial.log del serial.log

REM Start QEMU with:
REM   -monitor tcp:localhost:4444,server=on  (monitor on TCP port 4444)
REM   -serial file:serial.log                  (kernel log output)
start /B "" qemu-system-i386 -drive format=raw,file=build\os.img -m 128 -monitor tcp:localhost:4444,server=on,wait=off -serial file:serial.log -no-reboot -no-shutdown -display gtk 2>qemu_err.log

echo [TEST] Waiting for QEMU to start...
timeout /t 3 /nobreak >nul

echo [TEST] Waiting for OS to boot (~8 seconds)...
timeout /t 8 /nobreak >nul

echo [TEST] Injecting keystrokes via QEMU monitor...

REM Send "ls" command (sendkey format: each key separated by spaces)
REM l s enter
powershell -Command "try { $tcp = New-Object System.Net.Sockets.TcpClient('localhost', 4444); $stream = $tcp.GetStream(); $reader = New-Object System.IO.StreamReader($stream); $writer = New-Object System.IO.StreamWriter($stream); $writer.AutoFlush = $true; Start-Sleep -Milliseconds 500; $writer.WriteLine('sendkey l'); Start-Sleep -Milliseconds 100; $writer.WriteLine('sendkey s'); Start-Sleep -Milliseconds 100; $writer.WriteLine('sendkey ret'); Start-Sleep -Milliseconds 2000; $writer.WriteLine('quit'); $tcp.Close() } catch { Write-Host 'Error: $_' }"

echo [TEST] Commands sent. Waiting for response...
timeout /t 3 /nobreak >nul

echo.
echo ==========================================
echo  TEST RESULTS
echo ==========================================

if exist serial.log (
    echo.
    echo --- Serial Log (Kernel Output) ---
    type serial.log
    echo -----------------------------------
) else (
    echo [WARN] No serial.log generated!
)

if exist qemu_err.log (
    echo.
    echo --- QEMU Errors ---
    type qemu_err.log
    echo ---------------------
)

echo.
echo [TEST] Test complete. Press any key to exit.
pause >nul
