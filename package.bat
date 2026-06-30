@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo   Funs Core v0.7 - Package Script
echo ==========================================
echo.

REM Build everything
echo [1/4] Building kernel...
mingw32-make all
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

REM Create package directory
set PKG_DIR=funs-core-0.7
if exist %PKG_DIR% rmdir /s /q %PKG_DIR%
mkdir %PKG_DIR%
mkdir %PKG_DIR%\boot
mkdir %PKG_DIR%\kernel
mkdir %PKG_DIR%\docs

REM Copy files
echo [2/4] Copying files...
copy build\os.img %PKG_DIR%\boot\ >nul
copy build\kernel.elf %PKG_DIR%\kernel\ >nul
copy boot\linker.ld %PKG_DIR%\kernel\ >nul
copy Makefile %PKG_DIR%\ >nul
copy run.bat %PKG_DIR%\ >nul
copy debug.bat %PKG_DIR%\ >nul

REM Create README
echo [3/4] Creating README...
echo Funs Core v0.7 > %PKG_DIR%\README.txt
echo ================ >> %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo A 32-bit x86 operating system kernel. >> %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo Quick Start: >> %PKG_DIR%\README.txt
echo   1. Install QEMU (https://www.qemu.org/download/) >> %PKG_DIR%\README.txt
echo   2. Double-click run.bat to start >> %PKG_DIR%\README.txt
echo   3. For debugging, use debug.bat >> %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo Requirements: >> %PKG_DIR%\README.txt
echo   - QEMU (qemu-system-i386) >> %PKG_DIR%\README.txt
echo   - 128MB+ RAM for QEMU >> %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo Build from source: >> %PKG_DIR%\README.txt
echo   - mingw32-make (Windows) or make (Linux) >> %PKG_DIR%\README.txt
echo   - GCC with 32-bit support >> %PKG_DIR%\README.txt
echo   - NASM assembler >> %PKG_DIR%\README.txt

REM Create ZIP
echo [4/4] Creating archive...
where 7z >nul 2>&1
if %ERRORLEVEL% equ 0 (
    7z a -tzip %PKG_DIR%.zip %PKG_DIR%\ >nul
    echo [OK] Created %PKG_DIR%.zip
) else (
    where tar >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        tar -a -c -f %PKG_DIR%.zip -C . %PKG_DIR%
        echo [OK] Created %PKG_DIR%.zip
    ) else (
        echo [WARN] No archiver found. Package directory created at %PKG_DIR%\
    )
)

echo.
echo [DONE] Package complete!
pause
