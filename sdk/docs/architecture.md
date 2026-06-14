# FUNSOS Architecture

## Overview

FUNSOS is a 32-bit x86 operating system with a modular architecture:

```
┌─────────────────────────────────────────┐
│           User Applications             │
├─────────────────────────────────────────┤
│        SDK (funsos_api) / Renderer      │
├─────────────────────────────────────────┤
│         System API (sys_api.h)          │
├─────────────────────────────────────────┤
│              Kernel                     │
│  ┌──────┬──────┬──────┬──────┬───────┐ │
│  │Process│Memory│ FS   │ Net  │ GUI   │ │
│  │Mgmt  │Mgmt  │VFS   │Stack │Compos.│ │
│  └──────┴──────┴──────┴──────┴───────┘ │
├─────────────────────────────────────────┤
│          Hardware Drivers               │
│  ┌──────┬──────┬──────┬──────┬───────┐ │
│  │GPU   │NIC   │Audio │USB   │Storage│ │
│  └──────┴──────┴──────┴──────┴───────┘ │
├─────────────────────────────────────────┤
│          Boot / HAL                     │
└─────────────────────────────────────────┘
```

## Kernel Subsystems

### Process Management
- Multi-process with preemptive scheduling
- POSIX-compatible fork/exec/wait
- Signal handling (SIGINT, SIGTERM, SIGUSR1, etc.)
- Threading (pthreads)

### Memory Management
- Physical memory manager (PMM)
- Virtual memory manager (VMM) with paging
- Copy-on-write (COW) for fork
- Memory-mapped files (mmap)
- Swap support

### File System
- Virtual File System (VFS) layer
- Supported filesystems: ext2, ext4, FAT32, btrfs, XFS, ramfs, devfs, procfs, sysfs
- Journaling (ext4 journal)
- Block cache

### Networking
- Full TCP/IP stack
- Protocols: ARP, IP, ICMP, TCP, UDP, DNS, DHCP, HTTP
- Socket API (POSIX compatible)
- Firewall with bandwidth control

### GUI
- Window compositor
- 2D graphics engine (gfx.h)
- 3D rendering engine (gfx3d.h)
- Widget toolkit
- Theme engine
- Font rendering (FreeType mini)

### Database (FunDB)
- Embedded SQL database engine
- B-tree indexing
- WAL (Write-Ahead Logging) transactions
- SQL parser (SELECT/INSERT/UPDATE/DELETE/CREATE/DROP)

## Directory Structure

```
funsoS/
├── boot/        - Bootloader (stage1, stage2, loader)
├── kernel/      - Core kernel code
├── drivers/     - Hardware drivers
├── fs/          - Filesystem implementations
├── gui/         - GUI and graphics
├── net/         - Networking stack
├── audio/       - Audio subsystem
├── usb/         - USB stack
├── lib/         - C runtime library
├── apps/        - Built-in applications
├── userland/    - User-space utilities
├── os/          - Upper OS layer (desktop, apps, services)
├── renderer/    - Independent UI rendering engine
├── sdk/         - Software Development Kit
└── tools/       - Build tools
```

## System Call Interface

The kernel exposes services through `int $0x80` system calls.
The SDK provides a friendly C API wrapper around these calls.

## Build System

The project uses a Makefile-based build system:

```bash
make          # Build everything
make kernel   # Build kernel only
make apps     # Build user applications
make image    # Create OS image
make run      # Run in QEMU
```
