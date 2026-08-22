# FunsOS SDK - 软件开发工具包

<p align="center">
  <img src="https://img.shields.io/badge/SDK_Version-1.5.0-blue" alt="SDK Version"/>
  <img src="https://img.shields.io/badge/Target_OS-FunsOS_0.8-green" alt="Target OS"/>
  <img src="https://img.shields.io/badge/Kernel-FunsCore_v0.9-orange" alt="Kernel Version"/>
  <img src="https://img.shields.io/badge/Header_Files-38-orange" alt="Header Files"/>
  <img src="https://img.shields.io/badge/Examples-43-purple" alt="Examples Count"/>
</p>

<p align="center">
  <strong>FunsOS 官方软件开发工具包 —— 为 FunsOS 构建应用</strong><br/>
  <em>The official Software Development Kit for building applications on FunsOS</em>
</p>

---

**Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.**

---

## 简介

**FunsOS SDK** 是为 FunsOS 操作系统提供的官方软件开发工具包。它允许开发者使用 C 语言编写应用程序，通过 API 接口访问 FunsOS 的全部系统功能。

### SDK 组成

| 组件 | 说明 |
|------|------|
| **头文件 (`include/`)** | 38 个公共 API 头文件，涵盖窗口、图形、网络、音频、数据库等全部系统功能 |
| **运行时库 (`lib/`)** | CRT 启动代码和系统调用胶水层，将高级 API 调用翻译为底层 `int 0x80` 系统调用 |
| **示例程序 (`examples/`)** | 43 个完整示例，覆盖 SDK 的每个功能领域 |
| **构建工具 (`tools/`)** | `build_template.mk` 构建模板，简化应用程序编译流程 |

开发者只需 `#include "funsos.h"` 即可使用全部 SDK 功能。

---

## SDK 结构

```
sdk/
├── include/                   # SDK 公共头文件 (38 个)
│   ├── funsos.h              #   总头文件 — 一键包含全部子模块
│   │
│   │  核心 API
│   ├── funsos_window.h      #   窗口管理
│   ├── funsos_graphics.h    #   2D/3D 图形
│   ├── funsos_audio.h        #   音频播放
│   ├── funsos_network.h      #   网络通信
│   ├── funsos_files.h        #   文件 I/O
│   ├── funsos_process.h      #   进程管理
│   ├── funsos_memory.h       #   内存管理
│   ├── funsos_sysinfo.h      #   系统信息
│   ├── funsos_event.h        #   输入事件
│   ├── funsos_time.h         #   时间 API
│   ├── funsos_ipc.h          #   进程间通信
│   │
│   │  数据库与存储
│   ├── funsos_database.h     #   FunDB 数据库
│   ├── funsos_registry.h     #   系统注册表
│   │
│   │  安全与加密
│   ├── funsos_security.h     #   安全/权限/ACL
│   ├── funsos_crypto.h      #   加密/哈希/Base64
│   │
│   │  高级功能 (v1.5.0 新增)
│   ├── funsos_thread.h       #   线程管理 (pthread)
│   ├── funsos_signal.h       #   信号处理
│   ├── funsos_pipe.h         #   管道和 FIFO
│   ├── funsos_mmap.h         #   内存映射
│   ├── funsos_select.h       #   I/O 多路复用 (select/poll/epoll)
│   ├── funsos_json.h         #   JSON 解析
│   ├── funsos_compress.h    #   压缩/解压
│   ├── funsos_regex.h        #   正则表达式
│   ├── funsos_checksum.h     #   校验和 (CRC32/MD5/SHA)
│   ├── funsos_uuid.h         #   UUID 生成
│   ├── funsos_plugin.h       #   插件加载
│   ├── funsos_serial.h       #   串口通信
│   │
│   │  辅助模块
│   ├── funsos_libc.h         #   C 标准库兼容层
│   ├── funsos_logger.h       #   日志系统
│   ├── funsos_driver.h       #   驱动信息
│   ├── funsos_power.h       #   电源管理
│   ├── funsos_clipboard.h   #   剪贴板
│   ├── funsos_package.h     #   软件包管理
│   ├── funsos_errno.h        #   错误码定义
│   ├── funsos_dir.h          #   目录操作
│   ├── funsos_stat.h        #   文件状态
│   └── funsos_fs.h           #   文件系统高级操作
│
├── lib/                       # SDK 运行时库
│   ├── Makefile              #   运行时库构建脚本
│   ├── funsos_api.c         #   SDK 高级 API 实现
│   ├── funsos_glue.c        #   系统调用胶水层 (int 0x80 wrappers)
│   ├── funsos_window.c      #   窗口管理实现
│   ├── funsos_event.c       #   事件系统实现
│   ├── funsos_audio.c       #   音频系统实现
│   ├── funsos_surface.c     #   图形表面实现
│   ├── funsos_pipeline.c    #   3D 渲染管线实现
│   ├── funsos_libc.c        #   C 标准库实现
│   ├── funsos_logger.c      #   日志系统实现
│   ├── funsos_crypto.c      #   加密算法实现
│   ├── funsos_security.c    #   安全模块实现
│   └── ...
│
├── examples/                  # 示例程序 (43 个)
│   │
│   │  基础示例 (01-10)
│   ├── 01_hello/            #   Hello World 入门
│   ├── 02_window/           #   窗口创建与管理
│   ├── 03_drawing/          #   2D 图形绘制
│   ├── 04_3d_rendering/     #   3D 场景渲染
│   ├── 05_file_io/          #   文件读写操作
│   ├── 06_networking/        #   网络通信
│   ├── 07_audio/            #   音频播放
│   ├── 08_processes/        #   进程管理
│   ├── 09_signals/          #   信号处理
│   ├── 10_threads/          #   多线程编程
│   │
│   │  数据库与 UI (11-15)
│   ├── 11_database/         #   FunDB 数据库操作
│   ├── 12_custom_ui/        #   自定义 UI 控件
│   ├── 13_game/             #   游戏开发示例
│   ├── 14_fuse/            #   FUSE 用户文件系统
│   ├── 15_kvm/              #   KVM 虚拟化管理
│   │
│   │  进阶示例 (16-25)
│   ├── 16_3d_scene/         #   复杂 3D 场景
│   ├── 17_spreadsheet/       #   电子表格应用
│   ├── 18_chat/             #   聊天客户端
│   ├── 19_image_viewer/     #   图片查看器
│   ├── 20_package_mgr/       #   包管理器前端
│   ├── 21_system_info/       #   系统信息查询
│   ├── 22_ipc_demo/         #   进程间通信
│   ├── 23_fun_format/        #   Fun 格式处理
│   ├── 24_virtual_filesystem/ # 虚拟文件系统
│   ├── 25_network_server/    #   网络服务器
│   │
│   │  系统集成 (26-30)
│   ├── 25_power_mgmt/       #   电源管理
│   ├── 26_theme_demo/       #   主题定制
│   ├── 27_clipboard_demo/   #   剪贴板操作
│   ├── 28_notification/     #   系统通知
│   ├── 29_drag_drop/         #   拖放操作
│   ├── 29_thread_pool/       #   线程池示例
│   ├── 30_file_watcher/       #   文件监控
│   │
│   │  网络与数据 (31-34)
│   ├── 31_tcp_server/        #   TCP 服务器
│   ├── 32_http_client/       #   HTTP 客户端
│   ├── 33_json_demo/         #   JSON 解析
│   └── 34_checksum_demo/     #   校验和计算
│
└── tools/                    # 开发辅助工具
    ├── build_template.mk     #   应用程序 Makefile 模板
    └── funsdk-config.h       #   SDK 配置查询
```

---

## 头文件 API 参考

### 核心模块

| 序号 | 头文件 | 功能域 | 主要 API |
|:----:|--------|--------|----------|
| 1 | `funsos.h` | 总入口 | `#include "funsos.h"` 一键包含全部；版本号、错误码定义 |
| 2 | `funsos_window.h` | 窗口管理 | `window_create()` / `window_destroy()` / `window_move()` / `window_resize()` / `window_set_title()` / `window_show()` / `window_hide()` |
| 3 | `funsos_graphics.h` | 图形渲染 | 2D: `gfx_pixel()` / `gfx_line()` / `gfx_rect()` / `gfx_circle()` / `gfx_text()` / `gfx_blit()`; 3D: `gfx3d_*` 系列函数 |
| 4 | `funsos_audio.h` | 音频系统 | `audio_init()` / `audio_play_pcm()` / `audio_play_wav()` / `audio_stop()` / `audio_set_volume()` / `audio_get_device_list()` |
| 5 | `funsos_network.h` | 网络通信 | `socket()` / `bind()` / `listen()` / `accept()` / `connect()` / `send()` / `recv()` / `dns_resolve()` / `http_request()` |
| 6 | `funsos_files.h` | 文件 I/O | `open()` / `close()` / `read()` / `write()` / `lseek()` / `stat()` / `opendir()` / `readdir()` / `mkdir()` / `unlink()` |
| 7 | `funsos_process.h` | 进程管理 | `fork()` / `exec()` / `wait()` / `exit()` / `getpid()` / `getppid()` / `kill()` |
| 8 | `funsos_memory.h` | 内存管理 | `malloc()` / `free()` / `calloc()` / `realloc()` / `mmap()` / `munmap()` / `brk()` |
| 9 | `funsos_sysinfo.h` | 系统信息 | `sysinfo()` / `uname()` / `gethostname()` / `cpu_info()` / `mem_info()` / `uptime()` |
| 10 | `funsos_event.h` | 输入事件 | `event_poll()` / `event_wait()`; 键盘事件 (KEY_DOWN/KEY_UP); 鼠标事件 (MOUSE_MOVE/CLICK/WHEEL) |

### 扩展模块

| 序号 | 头文件 | 功能域 | 主要 API |
|:----:|--------|--------|----------|
| 11 | `funsos_database.h` | 数据库 | `fundb_open()` / `fundb_query()` / `fundb_exec()` / `fundb_close()` |
| 12 | `funsos_security.h` | 安全/权限 | `chmod()` / `chown()` / `getacl()` / `setacl()` / `check_permission()` |
| 13 | `funsos_crypto.h` | 加密/哈希 | `md5()` / `sha1()` / `sha256()` / `aes_encrypt()` / `aes_decrypt()` / `base64_encode()` / `base64_decode()` |
| 14 | `funsos_logger.h` | 日志系统 | `log_init()` / `log_debug()` / `log_info()` / `log_warn()` / `log_error()` |
| 15 | `funsos_power.h` | 电源管理 | `power_sleep()` / `power_reboot()` / `power_shutdown()` / `power_get_state()` |
| 16 | `funsos_clipboard.h` | 剪贴板 | `clipboard_set_text()` / `clipboard_get_text()` / `clipboard_clear()` |
| 17 | `funsos_registry.h` | 系统注册表 | `reg_open_key()` / `reg_get_value()` / `reg_set_value()` / `reg_close_key()` |
| 18 | `funsos_package.h` | 软件包管理 | `pkg_install()` / `pkg_remove()` / `pkg_search()` / `pkg_list()` |
| 19 | `funsos_libc.h` | C 标准库 | 字符串/内存/格式化输出等 libc 兼容函数 |
| 20 | `funsos_time.h` | 时间 API | `time()` / `gettimeofday()` / `clock_gettime()` / `sleep()` / `usleep()` |

### v1.5.0 新增模块

| 序号 | 头文件 | 功能域 | 主要 API |
|:----:|--------|--------|----------|
| 21 | `funsos_thread.h` | 线程管理 | `thread_create()` / `thread_join()` / `mutex_init()` / `cond_init()` / `barrier_init()` / `thread_pool_create()` |
| 22 | `funsos_signal.h` | 信号处理 | `sigaction()` / `sigprocmask()` / `sigpending()` / `sigsuspend()` / `kill()` / `raise()` |
| 23 | `funsos_pipe.h` | 管道/FIFO | `pipe()` / `pipe2()` / `mkfifo()` / `popen()` |
| 24 | `funsos_mmap.h` | 内存映射 | `mmap()` / `munmap()` / `mprotect()` / `msync()` / `mlock()` |
| 25 | `funsos_select.h` | I/O 多路复用 | `select()` / `poll()` / `epoll_create()` / `epoll_ctl()` / `epoll_wait()` |
| 26 | `funsos_json.h` | JSON 解析 | `json_parse()` / `json_object_get()` / `json_array_get()` / `json_dumps()` |
| 27 | `funsos_compress.h` | 压缩/解压 | `compress()` / `uncompress()` / `gzip_compress()` / `gzip_decompress()` |
| 28 | `funsos_regex.h` | 正则表达式 | `regcomp()` / `regexec()` / `regfree()` |
| 29 | `funsos_checksum.h` | 校验和 | `crc32()` / `crc16()` / `md5_file()` / `sha1_file()` / `sha256_file()` / `adler32()` |
| 30 | `funsos_uuid.h` | UUID 生成 | `uuid_generate()` / `uuid_parse()` / `uuid_unparse()` / `uuid_compare()` |
| 31 | `funsos_plugin.h` | 插件加载 | `plugin_load()` / `plugin_unload()` / `plugin_get_symbol()` / `plugin_list()` |
| 32 | `funsos_serial.h` | 串口通信 | `serial_open()` / `serial_set_config()` / `serial_read()` / `serial_write()` / `serial_flush()` |
| 33 | `funsos_errno.h` | 错误码 | 扩展错误码定义、`strerror()` / `perror()` |
| 34 | `funsos_dir.h` | 目录操作 | `mkdirp()` / `rmdir_r()` / `dir_walk()` / `dir_exists()` |
| 35 | `funsos_stat.h` | 文件状态 | `stat()` / `fstat()` / `lstat()` / `statfs()` / `utime()` |
| 36 | `funsos_fs.h` | 文件系统高级 | `inotify_init()` / `file_copy()` / `file_move()` / `getxattr()` / `setxattr()` |
| 37 | `funsos_ipc.h` | 进程间通信 | `shmget()` / `shmat()` / `msgget()` / `sem_init()` |
| 38 | `funsos_driver.h` | 驱动信息 | `lscpu()` / `lsmod()` / `driver_info()` |

---

## 版本信息

```c
// SDK 版本 (定义于 funsos.h)
#define FUNSOS_SDK_VERSION_MAJOR  1
#define FUNSOS_SDK_VERSION_MINOR  5
#define FUNSOS_SDK_VERSION_PATCH  0
#define FUNSOS_SDK_VERSION "1.5.0"

// 版本检查宏
#define FUNSOS_VERSION_CODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define FUNSOS_SDK_VERSION_CODE  FUNSOS_VERSION_CODE(1, 5, 0)

// 目标操作系统信息
#define FUNSOS_OS_NAME    "FUNSOS"
#define FUNSOS_KERNEL_NAME   "FunsCore"
#define FUNSOS_KERNEL_VERSION "0.8"
```

---

## 示例列表

### 基础示例 (01-10)

| 序号 | 示例名称 | 目录 | 描述 |
|:----:|----------|------|------|
| 01 | Hello World | `01_hello/` | 最基础的程序入口，打印 "Hello, FunsOS!" |
| 02 | Window Demo | `02_window/` | 创建窗口、设置标题、移动/调整大小、关闭回调 |
| 03 | Drawing Demo | `03_drawing/` | 2D 图形绘制：线、矩形、圆、文字、渐变填充 |
| 04 | 3D Cube | `04_3d_rendering/` | 旋转 3D 立方体渲染，透视投影与光照 |
| 05 | File I/O | `05_file_io/` | 文件的创建、读写、搜索、目录遍历 |
| 06 | Network Demo | `06_networking/` | TCP 服务器/客户端、HTTP 请求、DNS 解析 |
| 07 | Audio Player | `07_audio/` | WAV 文件播放、PCM 流式播放、音量控制 |
| 08 | Process Mgmt | `08_processes/` | fork/exec/wait 进程生命周期管理 |
| 09 | Signal Handling | `09_signals/` | 信号捕获与处理 (SIGINT/SIGTERM/SIGALRM) |
| 10 | Threading | `10_threads/` | pthread 创建、同步、Join、TLS 使用 |

### 数据库与 UI (11-15)

| 序号 | 示例名称 | 目录 | 描述 |
|:----:|----------|------|------|
| 11 | Database | `11_database/` | FunDB SQL 数据库 CRUD 操作 |
| 12 | Custom UI | `12_custom_ui/` | 使用 FunRender 创建自定义 UI 界面 |
| 13 | Game Demo | `13_game/` | 简单游戏：事件循环 + 渲染 + 碰撞检测 |
| 14 | FUSE Demo | `14_fuse/` | 用户空间文件系统挂载与操作 |
| 15 | KVM Demo | `15_kvm/` | KVM 虚拟机创建与管理 |

### 进阶示例 (16-25)

| 序号 | 示例名称 | 目录 | 描述 |
|:----:|----------|------|------|
| 16 | 3D Scene | `16_3d_scene/` | 复杂 3D 场景：多模型、多光源、纹理映射 |
| 17 | Spreadsheet | `17_spreadsheet/` | 电子表格应用：单元格编辑、公式计算 |
| 18 | Chat Client | `18_chat/` | 基于 TCP Socket 的聊天室客户端 |
| 19 | Image Viewer | `19_image_viewer/` | JPEG/PNG 图片打开、缩放、浏览 |
| 20 | Package Manager | `20_package_mgr/` | 包管理器前端界面 |
| 21 | System Info | `21_system_info/` | 系统信息查询：CPU、内存、磁盘、网络 |
| 22 | IPC Demo | `22_ipc_demo/` | 共享内存、消息队列、信号量 |
| 23 | Fun Format | `23_fun_format/` | Fun 格式文件处理演示 |
| 24 | Virtual FS | `24_virtual_filesystem/` | 虚拟文件系统演示 |
| 25 | Network Server | `25_network_server/` | 多客户端网络服务器 |

### v1.5.0 新增示例 (26-34)

| 序号 | 示例名称 | 目录 | 描述 |
|:----:|----------|------|------|
| 26 | Power Mgmt | `25_power_mgmt/` | 电源管理演示 |
| 27 | Theme Demo | `26_theme_demo/` | UI 主题定制演示 |
| 28 | Clipboard | `27_clipboard_demo/` | 剪贴板操作演示 |
| 29 | Notification | `28_notification/` | 系统通知演示 |
| 30 | Drag & Drop | `29_drag_drop/` | 拖放操作演示 |
| 31 | Thread Pool | `29_thread_pool/` | 线程池实现与任务调度 |
| 32 | File Watcher | `30_file_watcher/` | 文件系统变化监控 |
| 33 | TCP Server | `31_tcp_server/` | 高性能 TCP 服务器 |
| 34 | HTTP Client | `32_http_client/` | HTTP 客户端请求 |
| 35 | JSON Demo | `33_json_demo/` | JSON 解析与构造 |
| 36 | Checksum | `34_checksum_demo/` | 校验和计算演示 |

---

## 构建应用程序

### 方式一：使用构建模板

SDK 提供了标准的 `build_template.mk`，复制并修改即可：

```bash
# 复制模板
cp sdk/tools/build_template.mk your_app/Makefile

# 编辑 Makefile 修改 APP_NAME 和 SOURCES
# APP_NAME := my_app
# SOURCES  := my_app.c

# 构建
mingw32-make -f your_app/Makefile
```

### 方式二：手动编译

#### 第一步：设置 Include 路径

```c
/* 方法 A: 一键包含 (推荐) */
#include "funsos.h"

/* 方法 B: 按需包含 */
#include "funsos_window.h"
#include "funsos_graphics.h"
/* ... */
```

#### 第二步：编译

```bash
gcc -m32 -ffreestanding -nostdinc \
    -I sdk/include \
    -I sdk/../lib \
    -c your_app.c -o your_app.o
```

#### 第三步：链接

```bash
# 首先确保运行时库已构建
cd sdk/lib
mingw32-make          # 生成 libfunsos_api.a

# 然后链接应用程序
ld -m elf_i386 -o your_app.elf your_app.o \
   sdk/lib/libfunsos_api.a \
   -T sdk/../apps/crt0.o        # CRT 启动代码
```

### 第四步：部署到 FunsOS

将生成的 ELF 可执行文件放入 FunsOS 的 initrd 文件系统，然后重建 OS 镜像：

```bash
# 将应用放入 apps/ 目录或 initrd
# 然后重新构建整个项目
mingw32-make
```

### 交叉编译注意事项

| 注意事项 | 说明 |
|----------|------|
| **目标架构** | 必须使用 `-m32` 选项生成 32 位 ELF |
| **Freestanding** | 使用 `-ffreestanding -nostdlib -nostdinc` 标志 |
| **无标准库** | 不链接 glibc/msvcrt，所有 libc 功能由 SDK 提供 |
| **调用约定** | 遵循 System V ABI (cdecl) i386 调用约定 |
| **位置无关** | 使用 `-fno-pie -fno-pic` 禁止位置无关代码 |

---

## 运行时库

`sdk/lib/` 目录包含 FunsOS SDK 的 C 运行时库，它是连接用户态应用程序与内核之间的桥梁。

### 库组成

| 文件 | 描述 |
|------|------|
| `funsos_api.c` | SDK 高级 API 的实现层，提供面向对象风格的封装函数 |
| `funsos_glue.c` | **系统调用胶水层** — 将 C 函数调用转换为 `int 0x80` 汇编指令 |
| `Makefile` | 构建脚本，输出 `libfunsos_api.a` 静态库 |

### 工作原理

```
用户应用代码 (your_app.c)
    │  调用 window_create()
    ▼
SDK 头文件 (include/funsos_window.h)
    │  声明函数原型
    ▼
SDK 运行时库 (lib/funsos_api.c)
    │  参数封装与校验
    ▼
Syscall Glue (lib/funsos_glue.c)
    │  设置系统调用号到 EAX
    │  参数放入 EBX/ECX/EDX/ESI/EDI
    │  执行 INT 0x80
    ▼
内核 Syscall Dispatcher (kernel/syscall.c)
    │  根据 EAX 分发到具体处理函数
    ▼
内核实现 (kernel/syscall_impl.c)
    │  执行实际的系统操作
    ▔▶ 返回结果给用户态
```

### CRT 启动流程

当用户程序被 ELF 加载器加载后，执行流程如下：

```
_entry (apps/crt0.asm)
    │
    ├─► 清零 BSS 段
    ├─► 初始化栈指针 (ESP)
    ├─► 构造 argc/argv/envp
    ├─► 调用 __libc_init()     // C 运行时初始化
    │
    └─► 调用 main(argc, argv, envp)
            │
            └─► exit(main_return_value)  // _exit() 系统调用
```

---

## API 使用示例

### 示例 1: Hello World

```c
#include "funsos.h"

int main(int argc, char **argv) {
    printf("Hello, FunsOS!\n");
    printf("SDK Version: %s\n", FUNSOS_SDK_VERSION);
    printf("Kernel: %s %s\n", FUNSOS_KERNEL_NAME, FUNSOS_KERNEL_VERSION);
    return 0;
}
```

### 示例 2: 创建带按钮的窗口

```c
#include "funsos.h"

void on_button_click(void *widget, void *data) {
    printf("Button clicked!\n");
}

int main(int argc, char **argv) {
    /* 创建主窗口 */
    window_t *win = window_create(
        "My First App",     // 标题
        640, 480,           // 宽度, 高度
        WINDOW_RESIZABLE    // 窗口样式
    );

    /* 创建按钮 */
    widget_t *btn = button_create(
        win,
        "Click Me!",
        250, 200, 140, 40,  // x, y, width, height
        on_button_click,     // 点击回调
        NULL                 // 用户数据
    );

    /* 显示窗口并进入事件循环 */
    window_show(win);
    event_loop();            /* 阻塞直到窗口关闭 */

    window_destroy(win);
    return 0;
}
```

### 示例 3: 2D 图形绘制

```c
#include "funsos.h"

int main(int argc, char **argv) {
    /* 创建绘图窗口 */
    window_t *win = window_create("Drawing Demo", 800, 600, 0);
    canvas_t *canvas = window_get_canvas(win);

    /* 清空背景为白色 */
    canvas_clear(canvas, COLOR_WHITE);

    /* 绘制红色矩形 */
    canvas_set_color(canvas, COLOR_RED);
    canvas_fill_rect(canvas, 50, 50, 200, 150);

    /* 绘制蓝色圆形 */
    canvas_set_color(canvas, COLOR_BLUE);
    canvas_fill_circle(canvas, 450, 300, 80);

    /* 绘制绿色线条 */
    canvas_set_color(canvas, COLOR_GREEN);
    canvas_draw_line(canvas, 0, 0, 800, 600);

    /* 绘制文字 */
    canvas_set_color(canvas, COLOR_BLACK);
    canvas_draw_text(canvas, 300, 550,
        "Hello from FunsOS Graphics!", FONT_DEFAULT);

    /* 刷新显示 */
    canvas_present(canvas);
    window_show(win);
    event_loop();

    return 0;
}
```

### 示例 4: 文件读写

```c
#include "funsos.h"

int main(int argc, char **argv) {
    const char *filename = "/hello.txt";

    /* 写入文件 */
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to open file for writing");
        return 1;
    }

    const char *text = "Hello from FunsOS File I/O!\n";
    write(fd, text, strlen(text));
    close(fd);
    printf("Wrote to %s\n", filename);

    /* 读回文件 */
    fd = open(filename, O_RDONLY, 0);
    if (fd < 0) {
        perror("Failed to open file for reading");
        return 1;
    }

    char buffer[256];
    int bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    buffer[bytes_read] = '\0';
    close(fd);

    printf("Read back (%d bytes):\n%s\n", bytes_read, buffer);
    return 0;
}
```

### 示例 5: TCP 网络客户端

```c
#include "funsos.h"

int main(int argc, char **argv) {
    /* 创建 TCP Socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* DNS 解析 */
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);

    if (dns_resolve("example.com", &server.sin_addr) != 0) {
        fprintf(stderr, "DNS resolution failed\n");
        close(sock);
        return 1;
    }

    /* 连接服务器 */
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    /* 发送 HTTP 请求 */
    const char *request =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n\r\n";
    send(sock, request, strlen(request), 0);

    /* 接收响应 */
    char response[4096];
    int total = 0;
    int n;
    while ((n = recv(sock, response + total,
                      sizeof(response) - total - 1, 0)) > 0) {
        total += n;
    }
    response[total] = '\0';
    printf("Received %d bytes:\n%s\n", total, response);

    close(sock);
    return 0;
}
```

### 示例 6: FunDB 数据库

```c
#include "funsos.h"

int main(int argc, char **argv) {
    /* 打开数据库 */
    fundb_t *db = fundb_open("/data/myapp.db");
    if (!db) {
        fprintf(stderr, "Failed to open database\n");
        return 1;
    }

    /* 创建表 */
    fundb_exec(db, "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT, "
        "email TEXT)"
    );

    /* 插入数据 */
    fundb_exec(db, "INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
    fundb_exec(db, "INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')");

    /* 查询数据 */
    fundb_result_t *result = fundb_query(db, "SELECT * FROM users");
    while (fundb_result_next(result)) {
        printf("User: %s <%s>\n",
            fundb_column_text(result, 1),
            fundb_column_text(result, 2));
    }
    fundb_result_free(result);

    fundb_close(db);
    return 0;
}
```

---

## SDK 能力矩阵

| 能力域 | 支持程度 | 备注 |
|--------|:--------:|------|
| 窗口管理 | 完整 | 创建/销毁/移动/调整/层级/焦点 |
| 2D 图形 | 完整 | 基元、文字、图像、Blit |
| 3D 渲染 | 完整 | 网格、光照、纹理、相机 |
| 音频播放 | 完整 | PCM/WAV、混音、音量 |
| TCP/UDP 网络 | 完整 | Socket API、DNS、HTTP |
| 文件 I/O | 完整 | POSIX 兼容 |
| 进程管理 | 完整 | fork/exec/wait/exit |
| 线程管理 | 完整 | pthread/互斥/条件变量/线程池 |
| 内存管理 | 完整 | malloc/mmap/brk |
| 数据库 | 完整 | FunDB SQL |
| 输入事件 | 完整 | 键盘/鼠标/定时器 |
| FUSE | 完整 | 用户文件系统 |
| IPC | 完整 | 共享内存/消息队列/信号量 |
| 安全/权限 | 完整 | ACL/权限检查/chmod |
| 加密/哈希 | 完整 | MD5/SHA/AES/Base64 |
| 压缩/解压 | 完整 | zlib 风格 |
| JSON 解析 | 完整 | 解析/构造 |
| 正则表达式 | 完整 | POSIX 正则风格 |
| 校验和 | 完整 | CRC32/MD5/SHA1/SHA256 |
| I/O 多路复用 | 完整 | select/poll/epoll |
| 信号处理 | 完整 | sigaction/sigprocmask |
| 管道/FIFO | 完整 | pipe/mkfifo |
| UUID 生成 | 完整 | 随机/时间型 UUID |
| 串口通信 | 完整 | RS232/串口配置 |
| 插件加载 | 完整 | 动态加载/符号解析 |
| 日志系统 | 完整 | 分级日志 |
| 电源管理 | 完整 | 睡眠/重启/关机 |
| 剪贴板 | 完整 | 文本复制/粘贴 |
| 软件包管理 | 完整 | 安装/卸载/搜索 |
| KVM 虚拟化 | 完整 | 虚拟机创建/管理 |

---

<p align="center">
  <strong>FunsOS SDK v1.5.0 — FunsOS 应用程序开发工具包</strong><br/>
  <em>FunsOS SDK v1.5.0 — Software Development Kit for FunsOS Applications</em>
</p>
