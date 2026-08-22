# FunsCore / FunsOS

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.9-blue" alt="Version"/>
  <img src="https://img.shields.io/badge/Platform-x86_32bit-green" alt="Platform"/>
  <img src="https://img.shields.io/badge/Language-C%2FASM-orange" alt="Language"/>
  <img src="https://img.shields.io/badge/License-MIT-blue" alt="License"/>
</p>

<p align="center">
  <strong>从零构建、功能完备的 x86 32 位操作系统</strong><br/>
  <em>A from-scratch, feature-rich x86 32-bit operating system</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Kernel-FunsCore-v0.9-orange" alt="Kernel"/>
  <img src="https://img.shields.io/badge/SDK-v1.5.0-purple" alt="SDK"/>
  <img src="https://img.shields.io/badge/Renderer-FunRender-blue" alt="Renderer"/>
</p>

---

<p align="center">
  <strong>English</strong> | <a href="docs/README.md">简体中文文档</a>
</p>

---

## 项目简介

**FunsCore** 是本项目的内核名称（**v0.9**），**FunsOS** 是基于 FunsCore 构建的完整操作系统。这是一个从零开始、不依赖任何现有操作系统代码的 **x86 32 位操作系统项目**，使用 **C 语言和 x86 汇编语言**编写。

FunsOS 实现了现代操作系统的核心子系统：

- 进程调度与多任务管理
- 虚拟内存管理 (VMM)
- 完整的网络协议栈 (TCP/IP)
- 窗口管理与图形桌面
- 音频系统
- 文件系统 (VFS 层)
- 完整的用户态开发环境

该项目旨在展示操作系统底层原理，同时提供一个可实际运行、具备图形桌面环境的完整操作系统。

> **Documentation**: Naming conventions see [`docs/NAMING_CONVENTIONS.md`](docs/NAMING_CONVENTIONS.md). Comment style see [`docs/COMMENT_STYLE.md`](docs/COMMENT_STYLE.md). Roadmap see [`docs/ROADMAP.md`](docs/ROADMAP.md).

---

## 功能特性

### 内核核心

| 特性 | 描述 |
|------|------|
| **进程调度器** | 抢占式多任务调度，支持时间片轮转、优先级调度、CFS 公平调度策略 |
| **内存管理** | 物理内存页框分配 (PMM)、内核堆分配 (kheap)、伙伴系统算法 |
| **虚拟内存** | 页表映射、写时复制 (COW)、按需分页、内存映射文件 (mmap) |
| **系统调用** | 200+ 系统调用接口，通过 int 0x80 / sysenter 快速进入内核态 |
| **SMP 多核支持** | AP 启动引导、核间中断 (IPI)、自旋锁/信号量同步原语 |
| **异常处理** | Page Fault、General Protection Fault、Double Fault 等完整异常处理链 |
| **中断管理** | 可编程中断控制器 (PIC/APIC)、8259 PIT 定时器中断 |
| **线程支持** | POSIX 线程接口实现，包含 TLS（线程本地存储）支持 |
| **信号机制** | Unix 风格信号处理：SIGKILL、SIGTERM、SIGSEGV 等 |
| **进程间通信** | 消息队列 (msg)、共享内存 (shm)、管道 (pipe) |
| **ELF 加载器** | 支持动态链接 ELF 可执行文件的加载与执行 |

### 文件系统

| 文件系统 | 描述 |
|----------|------|
| **RAMFS** | 内存文件系统，用于 `/tmp` 和临时数据存储 |
| **FAT32** | 兼容 MS-DOS/Windows 的 FAT32 文件系统，支持长文件名 |
| **EXT2/EXT4** | Linux 经典文件系统，含日志 (journal) 回放功能 |
| **DEVFS** | 设备文件系统，自动生成 `/dev` 下设备节点 |
| **PROCFS** | 进程信息文件系统 (`/proc`)，提供运行时系统状态 |
| **SYSFS** | 系统设备与驱动信息文件系统 (`/sys`) |
| **FUSE** | 用户空间文件系统框架，允许用户态实现自定义文件系统 |

**通用 VFS 层**：统一的虚拟文件系统抽象层，提供 `open/read/write/close/lseek/stat` 等 POSIX 兼容接口。

### 网络协议栈

| 组件 | 描述 |
|------|------|
| **TCP/IP 协议栈** | 完整的 TCP、UDP、IP、ICMP、IGMP、ARP 实现 |
| **Socket 接口** | BSD Socket API 兼容，支持 TCP/UDP/RAW 三种套接字类型 |
| **防火墙** | 基于 Netfilter 的包过滤防火墙，支持规则链和带宽限制 |
| **DNS 解析器** | 客户端 DNS 查询与缓存 |
| **DHCP 客户端** | 自动获取 IP 地址、网关、DNS 服务器配置 |
| **HTTP 客户端** | 内置 HTTP/1.1 客户端库，支持 GET/POST 请求 |

### 图形系统

| 组件 | 描述 |
|------|------|
| **VBE 帧缓冲控制台** | VESA BIOS Extensions (VBE) 模式设置，高分辨率帧缓冲控制台 |
| **2D 图形引擎 (GFX)** | 像素绘制、线条、矩形、圆、椭圆、多边形填充 |
| **3D 软件渲染器 (GFX3D)** | 透视投影变换、Z-buffer 深度测试、Phong 光照模型、纹理映射 |
| **窗口管理器** | 窗口创建/销毁/移动/调整大小、Z-order 管理、焦点管理 |
| **合成器** | Alpha 混合合成、窗口阴影、圆角窗口 |
| **主题系统** | 可切换的 UI 主题（9 套内置主题） |

### 音频系统

| 组件 | 描述 |
|------|------|
| **Intel HDAudio** | High Definition Audio 总线驱动，支持多声道输出 |
| **AC97** | Audio Codec '97 兼容声卡驱动 |
| **CS4281/ES1370** | 声卡驱动 |
| **软件混音** | 多路音频流混合输出，音量控制 |

### 安全系统

| 特性 | 描述 |
|------|------|
| **Stack Canary** | 栈缓冲区溢出检测，编译时插入 canary 值验证 |
| **权限系统** | 完整 Unix 风格 rwx 权限模型 + ACL 扩展访问控制 |
| **用户角色体系** | 四级用户角色：Sover (uid=0)、Admin (uid=1)、User (普通用户)、Nobody (uid=65534) |
| **密码认证** | 加盐密码哈希存储 (Salted Hash)，安全登录认证机制 |

### 数据库引擎 - FunDB

| 特性 | 描述 |
|------|------|
| **B-tree 索引** | 高效的 B-tree 索引结构，支持范围查询 |
| **SQL 解析器** | 内嵌 SQL 语法解析器，支持 SELECT/INSERT/UPDATE/DELETE |
| **WAL 事务** | Write-Ahead Logging 事务日志，保证 ACID 特性 |

### 用户系统

| 特性 | 描述 |
|------|------|
| **四类角色** | Sover（超级管理员）、Admin（管理员）、User（普通用户）、Nobody（匿名） |
| **密码认证** | 密码哈希存储与登录认证 |
| **图形登录** | 图形界面登录服务 |

---

## 屏幕截图

> 截图待添加 (Screenshots coming soon)

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│                    FunsOS Desktop                           │
│                                                             │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐                   │
│   │  文件   │  │  终端   │  │ 设置   │                     │
│   │  管理   │  │        │  │        │                     │
│   └─────────┘  └─────────┘  └─────────┘                   │
│                                                             │
│   ┌───────────────────────────────────────────┐             │
│   │                                           │             │
│   │         Welcome to FunsOS                  │             │
│   │                                           │             │
│   └───────────────────────────────────────────┘             │
│                                                             │
│   ┌──────────────────────────────────────────┐ [开始] [🔊][📶][📅] │
│   └──────────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────────┘
```

---

## 系统架构

```
funsos/
├── boot/                      # 引导程序
│   ├── boot.asm               #   第一阶段引导 (MBR)
│   ├── stage2.asm             #   第二阶段引导
│   └── linker.ld              #   内核链接脚本
│
├── kernel/                    # 内核核心源码
│   ├── main.c                 #   内核入口
│   ├── entry.asm              #   汇编入口
│   ├── sched.c/h              #   进程调度器
│   ├── process.c/h            #   进程/线程管理
│   ├── vmm.c/h               #   虚拟内存管理
│   ├── syscall*.c/h          #   系统调用
│   ├── exception.c/h          #   CPU 异常处理
│   └── ...
│
├── fs/                        # 文件系统
│   ├── vfs.c/h               #   虚拟文件系统
│   ├── ext2.c/h              #   EXT2 文件系统
│   ├── fat32.c/h             #   FAT32 文件系统
│   └── ...
│
├── net/                       # 网络协议栈
│   ├── tcp.c/h               #   TCP 协议
│   ├── ip.c/h                #   IP 协议
│   ├── socket.c/h            #   BSD Socket
│   └── ...
│
├── drivers/                   # 硬件驱动
│   ├── audio/                #   声卡驱动
│   ├── block/               #   块设备驱动
│   ├── net/                 #   网卡驱动
│   ├── usb/                 #   USB 驱动
│   └── ...
│
├── gui/                       # 图形用户界面
│   ├── gfx.c/h              #   2D 图形
│   ├── window.c/h           #   窗口管理
│   └── ...
│
├── renderer/                  # FunRender 渲染引擎
│   ├── include/             #   头文件
│   ├── src/                 #   源文件 (30 个模块)
│   └── themes/              #   主题定义
│
├── sdk/                       # FunsOS SDK
│   ├── include/             #   38 个 API 头文件
│   ├── lib/                 #   运行时库
│   └── examples/            #   43 个示例程序
│
├── apps/                      # 内核内置应用
│   ├── shell.c              #   Shell 命令解释器
│   ├── desktop.c            #   桌面环境
│   ├── terminal.c           #   终端模拟器
│   └── ...
│
├── os/                        # 操作系统上层组件
│   ├── desktop/             #   桌面组件
│   ├── apps/               #   应用程序
│   └── services/           #   系统服务
│
└── build/                     # 构建输出
    ├── kernel.bin           #   内核二进制
    └── os.img               #   完整磁盘镜像
```

---

## 快速开始

### 前置要求

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| **GCC** | x86 32-bit 支持 | C 语言编译 (`gcc -m32`) |
| **NASM** | 2.x+ | x86 汇编编译 |
| **mingw32-make** | 任何版本 | 构建系统 (Windows) |
| **QEMU** | 6.0+ | 模拟器运行环境 |
| **Python 3** | 3.6+ | 磁盘镜像打包脚本 |

### 安装依赖

```bash
# Windows (MSYS2)
pacman -S mingw-w64-i686-gcc nasm qemu-system-i386 make python

# Ubuntu / Debian
sudo apt install gcc nasm qemu-system-x86 make python3

# macOS (需要 Homebrew)
brew install gcc nasm qemu python3
```

### 构建项目

```bash
# 进入项目目录
cd funsos

# 清理旧的构建产物 (可选)
mingw32-make clean

# 构建
mingw32-make -j4

# 或使用所有 CPU 核心
mingw32-make -j$(nproc)    # Linux/macOS
mingw32-make -j%NUMBER_OF_PROCESSORS%   # Windows
```

### 在 QEMU 中运行

```bash
# Windows
run.bat

# Linux / macOS
./run.sh

# 或手动启动
qemu-system-i386 \
  -drive format=raw,file=build/os.img \
  -m 512M \
  -serial stdio
```

### 使用 GDB 调试

```bash
# Windows
debug.bat

# Linux / macOS
./debug.sh

# 或手动启动
# 终端 1: 启动 QEMU (等待 GDB 连接)
qemu-system-i386 -drive format=raw,file=build/os.img -m 512M -s -S

# 终端 2: 连接 GDB
gdb build/kernel.elf \
  -ex "target remote :1234" \
  -ex "break main" \
  -ex "continue"
```

---

## 开发指南

### 添加新功能

#### 1. 添加新的系统调用

```c
// 1. 在 kernel/syscall_impl.c 中实现具体逻辑
int sys_my_feature(int arg1, const char *arg2) {
    // 实现代码
    return 0;
}

// 2. 在 kernel/syscall.c 中注册调用号
[SYS_MY_FEATURE] = sys_my_feature,

// 3. 在 sdk/include/funsos_*.h 中添加 SDK 头文件声明
int my_feature(int arg1, const char *arg2);
```

#### 2. 添加新的驱动程序

```
drivers/
└── my_driver/
    ├── my_driver.c      # 驱动实现
    └── my_driver.h      # 驱动头文件
```

实现标准驱动接口：
```c
int my_driver_init(void);
int my_driver_read(int fd, void *buf, size_t count);
int my_driver_write(int fd, const void *buf, size_t count);
```

#### 3. 添加新的 GUI 应用

```c
#include "funsos.h"

int main(int argc, char **argv) {
    window_t *win = window_create("My App", 640, 480, 0);

    button_t *btn = button_create(win, "Click Me!",
        250, 200, 140, 40, on_click, NULL);

    window_show(win);
    event_loop();
    window_destroy(win);
    return 0;
}
```

编译后放入 initrd 即可运行。

### 编码规范

| 规范 | 说明 |
|------|------|
| **语言** | C99 标准 + GNU ASM 内联汇编 |
| **命名风格** | 函数/变量: `snake_case`; 宏定义: `UPPER_SNAKE_CASE`; 类型: `_t` 后缀 |
| **缩进** | 4 空格 (禁止 Tab) |
| **注释** | 中文注释为主，关键函数需有功能说明 |
| **错误处理** | 使用负值返回错误码，成功返回 0 或正值 |
| **内存管理** | 内核态: `kmalloc/kfree`; 用户态: `malloc/free` |
| **并发安全** | 共享数据必须加锁 (spinlock/mutex) |

---

## 硬件要求

### 最低配置

| 项目 | 最低要求 |
|------|----------|
| **CPU** | x86 兼容处理器 (支持 Protected Mode) |
| **内存 (RAM)** | 256 MB |
| **磁盘空间** | 64 MB |
| **显卡** | VESA VBE 2.0+ (至少 800x600x32bpp) |
| **输入设备** | PS/2 或 USB 键盘 + 鼠标 |

### 推荐配置

| 项目 | 推荐规格 |
|------|----------|
| **CPU** | 双核 x86_64 |
| **内存 (RAM)** | 512 MB - 1 GB |
| **显卡** | VESA VBE 3.0+ (1280x1024x32bpp 或更高) |
| **磁盘空间** | 256 MB+ |

### 支持的运行环境

| 环境 | 说明 |
|------|------|
| **QEMU** | 完整支持所有功能，推荐用于开发和测试 |
| **VirtualBox** | 基本支持 |
| **VMware** | 基本支持 |
| **真实硬件** | 理论上支持大多数标准 PC 硬件 |

---

## 项目结构

```
FunsCore/
├── README.md                  # 本文档
├── Makefile                  # 顶层构建文件
├── run.bat / run.sh          # QEMU 启动脚本
├── debug.bat / debug.sh      # GDB 调试脚本
│
├── boot/                     # 引导程序
├── kernel/                   # 内核核心
├── fs/                       # 文件系统
├── net/                      # 网络协议栈
├── drivers/                  # 硬件驱动
├── gui/                      # 图形界面
├── renderer/                 # FunRender 渲染引擎
├── sdk/                      # SDK 开发工具包
├── apps/                     # 内置应用
├── os/                       # 操作系统组件
├── userland/                 # 用户态程序
├── lib/                      # C 运行库
├── usb/                      # USB 子系统
├── audio/                    # 音频子系统
├── gpu/                      # GPU 相关
├── docs/                     # 开发文档
├── tools/                    # 构建工具
└── build/                    # 构建输出
```

---

## 相关项目

| 项目 | 说明 |
|------|------|
| **FunsOS SDK** | FunsOS 应用程序开发工具包 |
| **FunRender** | 独立 UI 控件与渲染引擎 |
| **FunDB** | 内嵌数据库引擎 |

---

## 致谢

本项目在开发过程中参考了以下开源项目和学术资源：

| 项目 | 参考内容 |
|------|----------|
| **JamesM's kernel development tutorials** | 内核基础架构 (GDT/IDT/ISR/Paging) |
| **OSDev.org Wiki** | x86 硬件规范、协议文档 |
| **Linux Kernel** | 文件系统/VFS 设计、网络协议栈架构 |
| **Intel SDM** | x86/x64 架构参考 |
| **QEMU Project** | 虚拟化平台 |

---

## 许可证

```
MIT License

Copyright (c) 2025-2026 Funs Liu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 联系方式

| 方式 | 信息 |
|------|------|
| **作者** | Funs Liu |
| **邮箱** | ldp20000-2@outlook.com |

---

<p align="center">
  <strong>FunsCore v0.9 — 从零构建的 x86 操作系统</strong><br/>
  <em>FunsCore v0.9 — An operating system built from scratch</em>
</p>
