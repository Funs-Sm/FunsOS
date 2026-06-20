# FunsOS SDK - 软件开发工具包

<p align="center">
  <img src="https://img.shields.io/badge/SDK_Version-1.2.0-blue" alt="SDK Version"/>
  <img src="https://img.shields.io/badge/Target_OS-FunsOS_0.6-green" alt="Target OS"/>
  <img src="https://img.shields.io/badge/Kernel-FunsCore_v0.6-orange" alt="Kernel Version"/>
  <img src="https://img.shields.io/badge/APIs-200%2B_Syscalls-red" alt="API Count"/>
  <img src="https://img.shields.io/badge/Examples-20%2B-purple" alt="Examples Count"/>
</p>

<p align="center">
  <strong>FunsOS 官方软件开发工具包 —— 为 FunsOS 构建应用的完整开发环境</strong><br/>
  <em>The official Software Development Kit for building applications on FunsOS</em>
</p>

---

**Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.**
Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.

---

## 简介

**FunsOS SDK** 是为 FunsOS 操作系统提供的软件开发工具包。它允许开发者使用 C 语言编写应用程序，通过 API 接口访问 FunsOS 的系统功能，包括图形窗口、2D/3D 渲染、音频播放、网络通信、文件操作、进程管理和数据库访问等。

SDK 采用**头文件 + 运行时库**的设计模式：
- **头文件 (`include/`)** 提供 10 个公共 API 头文件，涵盖全部系统功能的声明与数据类型定义
- **运行时库 (`lib/`)** 提供 CRT (C Runtime) 启动代码和系统调用胶水层 (syscall glue)，将高级 API 调用翻译为底层的 `int 0x80` 系统调用
- **示例程序 (`examples/`)** 包含 27+ 个完整示例，覆盖 SDK 的每个功能领域
- **辅助工具 (`tools/`)** 提供 `funsdk-config` 配置工具和 `build_template.mk` 构建模板
- **文档 (`docs/`)** 包含 API 参考手册、架构说明和快速入门指南

开发者只需 `#include "funsos.h"` 即可获得对全部 SDK 功能的访问权。

---

## SDK 结构

```
sdk/
├── README.md                  # SDK 说明文档
│
├── include/                   # SDK 公共头文件 (10 个)
│   ├── funsos.h               #   总头文件 (一键包含全部子模块)
│   ├── funsos_window.h        #   窗口管理 API
│   ├── funsos_graphics.h      #   图形绘制 API (2D + 3D)
│   ├── funsos_audio.h         #   音频播放 API
│   ├── funsos_network.h       #   网络通信 API
│   ├── funsos_files.h         #   文件 I/O API
│   ├── funsos_process.h       #   进程/线程 API
│   ├── funsos_memory.h        #   内存管理 API
│   ├── funsos_sysinfo.h       #   系统信息 API
│   └── funsos_events.h        #   输入事件 API
│
├── lib/                       # SDK 运行时库
│   ├── Makefile               #   运行时库构建脚本
│   ├── funsos_api.c           #   SDK 高级 API 实现
│   ├── funsos_api.h           #   运行时库内部头文件
│   └── funsos_glue.c          #   系统调用胶水层 (syscall wrappers)
│
├── examples/                  # 示例程序 (20 个)
│   ├── 01_hello/              #   Hello World 入门
│   │   └── hello.c
│   ├── 02_window/             #   窗口创建与管理
│   │   └── window.c
│   ├── 03_drawing/            #   2D 图形绘制
│   │   └── drawing.c
│   ├── 04_3d_rendering/       #   3D 场景渲染
│   │   └── cube.c
│   ├── 05_file_io/            #   文件读写操作
│   │   └── fileio.c
│   ├── 06_networking/         #   网络通信
│   │   └── netdemo.c
│   ├── 07_audio/              #   音频播放
│   │   └── audio.c
│   ├── 08_processes/          #   进程管理 (fork/exec/wait)
│   │   └── process.c
│   ├── 09_signals/            #   信号处理
│   │   └── signals.c
│   ├── 10_threads/            #   多线程编程
│   │   └── threads.c
│   ├── 11_database/           #   FunDB 数据库操作
│   │   └── database.c
│   ├── 12_custom_ui/          #   自定义 UI 控件
│   │   └── custom_ui.c
│   ├── 13_game/               #   游戏开发示例
│   │   └── game.c
│   ├── 14_fuse/               #   FUSE 用户文件系统
│   │   └── fuse_demo.c
│   ├── 15_kvm/                #   KVM 虚拟化管理
│   │   └── kvm_demo.c
│   ├── 16_3d_scene/           #   复杂 3D 场景
│   │   └── scene.c
│   ├── 17_spreadsheet/        #   电子表格应用
│   │   └── spreadsheet.c
│   ├── 18_chat/               #   聊天客户端
│   │   └── chat.c
│   ├── 19_image_viewer/       #   图片查看器
│   │   └── imgview.c
│   └── 20_package_mgr/        #   包管理器前端
│       └── pkgmgr.c
│
├── tools/                     # 开发辅助工具
│   ├── funsdk-config.h        #   SDK 配置查询工具头文件
│   └── build_template.mk      #   应用程序 Makefile 模板
│
└── docs/                      # SDK 文档
    ├── getting_started.md     #   快速入门指南
    ├── architecture.md        #   SDK 架构说明
    └── api_reference.md       #   API 完整参考手册
```

---

## 头文件 API 参考

以下列出 SDK 提供的全部 10 个头文件及其功能覆盖范围：

| 序号 | 头文件 | 功能域 | 主要 API 概览 |
|:----:|--------|--------|---------------|
| **1** | **`funsos.h`** | 总入口 | 一键 `#include` 全部子模块；定义 SDK 版本号、OS 名称、内核版本常量 |
| **2** | **`funsos_window.h`** | 窗口管理 | `window_create()` / `window_destroy()` / `window_move()` / `window_resize()` / `window_set_title()` / `window_show()` / `window_hide()`; 窗口属性、事件回调 |
| **3** | **`funsos_graphics.h`** | 图形渲染 | 2D: `gfx_pixel()` / `gfx_line()` / `gfx_rect()` / `gfx_circle()` / `gfx_text()` / `gfx_blit()`; 3D: `gfx3d_init()` / `gfx3d_load_mesh()` / `gfx3d_set_camera()` / `gfx3d_render()` / `gfx3d_set_light()`; 图像: `png_load()` / `jpeg_load()` |
| **4** | **`funsos_audio.h`** | 音频系统 | `audio_init()` / `audio_play_pcm()` / `audio_play_wav()` / `audio_stop()` / `audio_set_volume()` / `audio_get_device_list()` |
| **5** | **`funsos_network.h`** | 网络通信 | `socket()` / `bind()` / `listen()` / `accept()` / `connect()` / `send()` / `recv()` / `dns_resolve()` / `http_request()` / `gethostbyname()` |
| **6** | **`funsos_files.h`** | 文件 I/O | `open()` / `close()` / `read()` / `write()` / `lseek()` / `stat()` / `opendir()` / `readdir()` / `mkdir()` / `unlink()` / `rename()` |
| **7** | **`funsos_process.h`** | 进程/线程 | `fork()` / `exec()` / `wait()` / `exit()` / `getpid()` / `getppid()`; `thread_create()` / `thread_join()` / `thread_exit()`; `kill()` / `signal()` / `sigaction()` |
| **8** | **`funsos_memory.h`** | 内存管理 | `malloc()` / `free()` / `calloc()` / `realloc()` / `mmap()` / `munmap()` / `brk()` / `sbrk()` |
| **9** | **`funsos_sysinfo.h`** | 系统信息 | `sysinfo()` / `uname()` / `gethostname()` / `cpu_info()` / `mem_info()` / `uptime()` / `loadavg()` |
| **10** | **`funsos_events.h`** | 输入事件 | `event_poll()` / `event_wait()`; 键盘事件 (KEY_DOWN/KEY_UP/KEY_PRESS); 鼠标事件 (MOUSE_MOVE/MOUSE_CLICK/MOUSE_WHEEL); 定时器事件 (TIMER_TICK) |

### 版本信息

```c
// SDK 版本 (定义于 funsos.h)
#define FUNSOS_SDK_VERSION_MAJOR  1
#define FUNSOS_SDK_VERSION_MINOR  0
#define FUNSOS_SDK_VERSION_PATCH  0
#define FUNSOS_SDK_VERSION "1.0.0"

// 目标操作系统信息
#define FUNSOS_OS_NAME       "FUNSOS"
#define FUNSOS_KERNEL_NAME   "FunsCore"
#define FUNSOS_KERNEL_VERSION "0.6"
```

---

## 示例列表

SDK 提供了 **20 个示例程序**，从最基础的 Hello World 到复杂的 3D 场景渲染和网络聊天客户端：

| 序号 | 示例名称 | 目录 | 描述 | 使用的主要 API |
|:----:|----------|------|------|----------------|
| **01** | Hello World | `01_hello/` | 基础的程序入口，打印 "Hello, FunsOS!" | `printf()`, 基础 I/O |
| **02** | Window Demo | `02_window/` | 创建窗口、设置标题、移动/调整大小、关闭回调 | `window_create()`, `funsos_window.h` |
| **03** | Drawing Demo | `03_drawing/` | 2D 图形绘制：线、矩形、圆、文字、渐变填充 | `gfx_line()`, `gfx_rect()`, `gfx_circle()`, `funsos_graphics.h` |
| **04** | 3D Cube | `04_3d_rendering/` | 旋转 3D 立方体渲染，透视投影与光照 | `gfx3d_load_mesh()`, `gfx3d_render()`, `gfx3d_set_light()` |
| **05** | File I/O | `05_file_io/` | 文件的创建、读写、搜索、目录遍历 | `open()`, `read()`, `write()`, `opendir()`, `funsos_files.h` |
| **06** | Network Demo | `06_networking/` | TCP 服务器/客户端、HTTP 请求、DNS 解析 | `socket()`, `connect()`, `send()`, `recv()`, `dns_resolve()`, `funsos_network.h` |
| **07** | Audio Player | `07_audio/` | WAV 文件播放、PCM 流式播放、音量控制 | `audio_play_wav()`, `audio_set_volume()`, `funsos_audio.h` |
| **08** | Process Mgmt | `08_processes/` | fork/exec/wait 进程生命周期管理 | `fork()`, `exec()`, `wait()`, `getpid()`, `funsos_process.h` |
| **09** | Signal Handling | `09_signals/` | 信号捕获与处理 (SIGINT/SIGTERM/SIGALRM) | `signal()`, `sigaction()`, `kill()`, `raise()` |
| **10** | Threading | `10_threads/` | pthread 创建、同步、Join、TLS 使用 | `thread_create()`, `thread_join()`, `mutex_lock()` |
| **11** | Database | `11_database/` | FunDB SQL 数据库 CRUD 操作 | `fundb_open()`, `fundb_query()`, `fundb_exec()` |
| **12** | Custom UI | `12_custom_ui/` | 使用 FunRender 创建自定义 UI 界面 | `fr_init()`, `fr_create_button()`, FunRender API |
| **13** | Game Demo | `13_game/` | 简单游戏 (事件循环 + 渲染 + 碰撞检测) | 图形 + 事件 + 定时器综合运用 |
| **14** | FUSE Demo | `14_fuse/` | 用户空间文件系统挂载与操作 | `fuse_mount()`, `fuse_register_ops()` |
| **15** | KVM Demo | `15_kvm/` | KVM 虚拟机创建与管理 | `kvm_create_vm()`, `kvm_start()`, `kvm_save_state()` |
| **16** | 3D Scene | `16_3d_scene/` | 复杂 3D 场景：多模型、多光源、纹理映射 | `gfx3d_*` 系列 API 全功能展示 |
| **17** | Spreadsheet | `17_spreadsheet/` | 电子表格应用：单元格编辑、公式计算 | 窗口 + 文件 + 图形组合应用 |
| **18** | Chat Client | `18_chat/` | 基于 TCP Socket 的聊天室客户端 | `socket()`, `connect()`, `select()`, UI 组合 |
| **19** | Image Viewer | `19_image_viewer/` | 图片浏览器：JPEG/PNG 打开、缩放、浏览 | `png_load()`, `jpeg_load()`, 窗口 + 图形 |
| **20** | Package Manager | `20_package_mgr/` | 包管理器前端界面：搜索/安装/卸载 | 网络 + 文件 + 进程 + UI 综合 |

---

## 构建应用程序

### 第一步：设置 Include 路径

在应用程序源码中引入 SDK：

```c
/* 方法 A: 一键包含 (推荐) */
#include "funsos.h"

/* 方法 B: 按需包含 */
#include "funsos_window.h"
#include "funsos_graphics.h"
#include "funsos_events.h"
/* ... */
```

编译时指定头文件搜索路径：

```bash
gcc -m32 -ffreestanding -nostdinc \
    -I sdk/include \
    -I sdk/../lib \
    -c your_app.c -o your_app.o
```

### 第二步：链接运行时库

首先确保运行时库已构建：

```bash
cd sdk/lib
mingw32-make          # 生成 libfunsos_api.a
```

然后链接应用程序：

```bash
ld -m elf_i386 -o your_app.elf your_app.o \
   sdk/lib/libfunsos_api.a \
   -T sdk/../apps/crt0.o        # CRT 启动代码
```

### 第三步：使用构建模板

SDK 提供了标准的 `build_template.mk`，复制并修改即可：

```bash
cp sdk/tools/build_template.mk your_app/Makefile
# 编辑 Makefile 修改 APP_NAME 和 SOURCES
mingw32-make -f your_app/Makefile
```

### 第四步：部署到 FunsOS

将生成的 ELF 可执行文件放入 FunsOS 的 initrd 文件系统中（通常位于 `/usr/bin/` 或 `/bin/`），然后重建 OS 镜像：

```bash
# 将应用放入 apps/ 目录或 initrd
# 然后重新构建整个项目
mingw32-make
```

### 交叉编译注意事项

| 注意事项 | 说明 |
|-----------|------|
| **目标架构** | 必须使用 `-m32` 选项生成 32 位 ELF |
| **Freestanding** | 使用 `-ffreestanding -nostdlib -nostdinc` 标志 |
| **无标准库** | 不链接 glibc/msvcrt，所有 libc 功能由 SDK 的 `lib/` 提供 |
| **调用约定** |遵循 System V ABI (cdecl) i386 调用约定 |
| **位置无关** | 使用 `-fno-pie -fno-pic` 禁止位置无关代码 |
| **栈保护** | 内核态编译禁用 `-fno-stack-protector`；用户态应用可以启用 |

---

## 运行时库

`sdk/lib/` 目录包含 FunsOS SDK 的 C 运行时库，它是连接用户态应用程序与内核之间的桥梁。

### 库组成

| 文件 | 描述 |
|------|------|
| **`funsos_api.c`** | SDK 高级 API 的实现层，提供面向对象风格的封装函数 |
| **`funsos_api.h`** | 运行时库内部使用的头文件与数据结构定义 |
| **`funsos_glue.c`** | **系统调用胶水层 (Syscall Glue)** — 将 C 函数调用转换为 `int 0x80` 汇编指令 |
| **`Makefile`** | 构建脚本，输出 `libfunsos_api.a` 静态库 |

### 工作原理

```
用户应用代码 (your_app.c)
    │  调用 funsos_window_create()
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

## 工具链

### funsdk-config

`funsdk-config.h` 提供编译时配置查询功能，允许应用在编译期获取 SDK 的路径和参数信息：

```c
#include "tools/funsdk-config.h"

// 获取 SDK 安装路径
const char *sdk_path = funsdk_config_get("prefix");     // => "/sdk"
const char *inc_path = funsdk_config_get("includedir"); // => "/sdk/include"
const char *lib_path = funsdk_config_get("libdir");     // => "/sdk/lib"
const char *version  = funsdk_config_get("version");   // => "1.0.0"

// 获取编译标志
const char *cflags = funsdk_config_get("cflags");
// => "-m32 -ffreestanding -nostdinc -I/sdk/include -I/lib"

// 获取链接标志
const char *ldflags = funsdk_config_get("ldflags");
// => "-m32 -L/sdk/lib -lfunsos_api"
```

### build_template.mk

以下是 SDK 提供的标准 Makefile 模板的核心部分：

```makefile
# ===== FunsOS Application Makefile Template =====
APP_NAME := my_app
SOURCES  := my_app.c

# SDK 路径 (根据实际情况修改)
SDK_DIR  := ../sdk

# 编译器与标志
CC       := gcc
CFLAGS   := -m32 -ffreestanding -nostdinc -nostdlib \
            -fno-builtin -fno-stack-protector \
            -Wall -Wextra \
            -I$(SDK_DIR)/include -I../lib

# 链接器
LD       := ld
LDFLAGS  := -m elf_i386 -nostdlib -T ../apps/crt0.o

# SDK 运行时库
SDK_LIB  := $(SDK_DIR)/lib/libfunsos_api.a

# 构建目标
TARGET   := $(APP_NAME).elf

# 默认目标
all: $(TARGET)

# 编译
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 链接
$(TARGET): $(SOURCES:.c=.o)
	$(LD) $(LDFLAGS) -o $@ $^ $(SDK_LIB)

# 清理
clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
```

---

## 文档

`sdk/docs/` 目录包含三份核心文档：

| 文档 | 路径 | 内容概述 |
|------|------|----------|
| **快速入门** | `docs/getting_started.md` | 从环境搭建到第一个 "Hello World" 的教程 |
| **架构说明** | `docs/architecture.md` SDK 的分层架构设计、API 设计哲学、内部实现原理 |
| **API 参考** | `docs/api_reference.md` | 每个 API 函数的详细签名、参数说明、返回值、示例代码 |

建议阅读顺序：**getting_started.md** -> **api_reference.md** -> **architecture.md**

---

## API 使用示例

### 示例 1: Hello World

```c
/* 最简单的 FunsOS 应用 */
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
    /* 可以在这里打开对话框、执行操作等 */
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
    
    const char *text = "Hello from FunsOS File I/O!\n"
                       "This is a test of the SDK file APIs.\n";
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
| 进程/线程 | 完整 | fork/exec/pthread/signal |
| 内存管理 | 完整 | malloc/mmap/brk |
| 数据库 | 完整 | FunDB SQL |
| 输入事件 | 完整 | 键盘/鼠标/定时器 |
| 虚拟化 | 完整 | KVM 管理 |
| FUSE | 完整 | 用户文件系统 |

---

<p align="center">
  <strong>FunsOS SDK v1.2.0 — FunsOS 应用程序开发工具包</strong><br/>
  <em>FunsOS SDK v1.2.0 — Software Development Kit for FunsOS Applications</em>
</p>