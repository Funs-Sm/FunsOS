# FunsOS v0.8.2 使用手册

> 本手册对应 FunsOS v0.8.2（内核版本 FunsCore v0.8.2），涵盖系统安装、桌面使用、网络配置、命令行工具和开发者接口。
>
> **目标读者**：希望实际使用 FunsOS 或基于 FunsCore 开发应用的用户和开发者。

---

## 目录

1. [系统概述](#1-系统概述)
2. [安装与启动](#2-安装与启动)
3. [用户系统](#3-用户系统)
4. [桌面环境](#4-桌面环境)
5. [文件系统](#5-文件系统)
6. [网络配置](#6-网络配置)
7. [命令行工具](#7-命令行工具)
8. [系统服务](#8-系统服务)
9. [开发者接口](#9-开发者接口)
10. [故障排除](#10-故障排除)

---

## 1. 系统概述

### 1.1 关于 FunsOS

FunsOS 是一个从零编写的 x86 32 位操作系统，使用 C 语言和 x86 汇编语言实现，不依赖任何现有操作系统代码。

**核心架构层次**：

```
┌─────────────────────────────────────────────┐
│           桌面环境 / 应用程序                  │
├─────────────────────────────────────────────┤
│   Shell  │ 窗口管理器  │ 文件管理器  │ 网络工具 │
├─────────────────────────────────────────────┤
│        200+ 系统调用接口 (int 0x80)          │
├─────────────────────────────────────────────┤
│ 内核核心: 进程调度 │ 内存管理 │ VFS │ 网络栈  │
├─────────────────────────────────────────────┤
│         设备驱动 (PCI/NIC/USB/Storage)        │
├─────────────────────────────────────────────┤
│              硬件 (x86, 32-bit)              │
└─────────────────────────────────────────────┘
```

### 1.2 主要子系统

| 子系统 | 实现规模 | 支持情况 |
|--------|----------|----------|
| 进程调度 | MLFQ / CFS / Deadline / RT | ✅ 生产可用 |
| 虚拟内存 | 伙伴系统 + 页表 + COW + mmap + swap | ✅ 生产可用 |
| 文件系统 | EXT2/EXT4/FAT32/RAMFS/DEVFS/PROCFS/SYSFS/BTRFS/XFS/TARFS/FUSE | ✅ EXT2/EXT4/FAT32/RAMFS 可用，其他部分实现 |
| TCP/IP 网络 | TCP/UDP/ICMP/IGMP/ARP + Socket API | ✅ 基本可用 |
| 防火墙 | Netfilter 五链 (PREROUTING/FORWARD/POSTROUTING/LOCAL_IN/LOCAL_OUT) | ✅ 已接入 |
| 图形系统 | VBE 帧缓冲 + 2D/3D 渲染 + 窗口合成器 | ✅ 基本可用 |
| 音频 | Intel HDAudio / AC97 / SB16 | ⚠️ 驱动已实现 |
| 用户认证 | Sover/Admin/User/Nobody 四级角色 | ✅ 可用 |

### 1.3 硬件需求

| 组件 | 最低要求 | 推荐 |
|------|----------|------|
| CPU | x86 32-bit (i486+) | Pentium II+ |
| 内存 | 32 MB | 128 MB+ |
| 硬盘 | 100 MB | 1 GB+ |
| 显卡 | VESA 2.0 (800x600) | VESA 3.0 (1920x1080) |
| 网卡 | RTL8139 / E1000 / Virtio-NET | Intel E1000 |

---

## 2. 安装与启动

### 2.1 获取镜像

使用 `mingw32-make` 从源码构建：

```bash
cd D:/Software/Project/5
mingw32-make -j4
```

构建产物：
- `build/os.img` — 完整磁盘镜像（~27 MB），可直接用于 QEMU 或写入磁盘
- `build/kernel.elf` — 内核 ELF 文件（用于调试）

### 2.2 启动方式

**QEMU 模拟器运行（推荐用于测试）**：

```bash
mingw32-make run
# 等价于:
qemu-system-i386 -drive format=raw,file=build/os.img,if=ide,index=0 -m 128 -serial stdio
```

**GDB 调试模式**：

```bash
mingw32-make debug
# 在另一个终端启动 GDB:
gdb -ex "target remote localhost:1234" build/kernel.elf
```

**烧录到物理磁盘**（需要管理员权限）：

```bash
# Windows (以管理员身份运行命令提示符):
dd if=build/os.img of=\\.\PhysicalDriveN bs=512 count=52910
```

### 2.3 启动流程

```
BIOS/UEFI → MBR (boot.asm) → Stage2 引导 → 保护模式 → 内核入口
    → 内存检测 → VFS 挂载 → 网络栈初始化 → 图形模式切换
    → 桌面环境 → 登录界面
```

---

## 3. 用户系统

### 3.1 用户角色

FunsOS 实现四级用户角色体系：

| 角色 | UID | 描述 | 权限 |
|------|-----|------|------|
| **Sover** | 0 | 超级管理员 | 完全控制，可操作所有资源 |
| **Admin** | 1 | 管理员 | 系统配置权限 |
| **User** | 1000+ | 普通用户 | 常规操作权限 |
| **Nobody** | 65534 | 匿名用户 | 最小权限 |

### 3.2 登录

系统启动后显示登录界面：

```
╔═══════════════════════════════════════════════╗
║              FunsOS v0.8.2                   ║
║              登录系统                         ║
╠═══════════════════════════════════════════════╣
║  用户名: [________________]                    ║
║  密码:   [________________]                  ║
║                                               ║
║           [ 登录 ]    [ 注册 ]               ║
╚═══════════════════════════════════════════════╝
```

**默认用户**：
- 用户名：`root`（Sover 角色，UID=0）
- 密码：空（首次登录时系统会引导设置密码）

### 3.3 用户管理命令

```bash
# 添加用户 (Admin/Sover)
adduser <username> [--uid N] [--gid N] [--shell /path/shell]

# 修改密码 (自己用 passwd，或 Sover/Admin 修改他人)
passwd [username]

# 删除用户 (Sover only)
deluser <username>

# 切换用户
su <username>

# 查看当前用户
whoami

# 查看用户信息
id [username]
```

### 3.4 文件权限

FunsOS 支持 Unix 风格权限模型：

```bash
# 查看文件权限
ls -l /home/user/file.txt
# 输出: -rw-r--r--  user  users  1234  Jan 1 12:00  file.txt

# 修改权限 (Sover/Admin)
chmod 755 /path/to/file
chmod u+x /path/to/script   # 给所有者加执行权限

# 修改所有者 (Sover only)
chown user:group /path/to/file
```

权限字符对照：
- `r` = 读 (4)
- `w` = 写 (2)
- `x` = 执行 (1)
- `u` = 所有者，`g` = 所属组，`o` = 其他用户，`a` = 所有

---

## 4. 桌面环境

### 4.1 桌面布局

```
┌──────────────────────────────────────────────────────────┐
│  FunsOS 桌面                      [网络] [声音] [时间]  │
│                                                          │
│                                                          │
│           [桌面图标区域]                                  │
│           📁 文件管理器                                   │
│           🖥 终端                                         │
│           ⚙️ 设置                                        │
│           📝 文本编辑器                                  │
│                                                          │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ [⊞] 开始菜单 │ [窗口按钮]  │  │ 13:42 │ [输入法] │
└──────────────────────────────────────────────────────────┘
```

### 4.2 窗口操作

| 操作 | 方式 |
|------|------|
| 移动窗口 | 拖动标题栏 |
| 调整大小 | 拖动窗口边缘 |
| 最小化 | 点击标题栏 `_` 按钮 |
| 最大化 | 点击标题栏 `□` 按钮 |
| 关闭 | 点击标题栏 `×` 按钮 |
| 切换焦点 | 点击窗口 / Alt+Tab |
| 置顶 | 右键标题栏 → 置顶 |

### 4.3 开始菜单

点击任务栏左侧 `⊞` 按钮打开开始菜单：

```
╔═══════════════════════════════╗
║ 🔍 搜索应用...                ║
╠═══════════════════════════════╣
║ 📁 文件管理器                 ║
║ 🖥 终端模拟器                 ║
║ ⚙️ 系统设置                   ║
║ 📝 文本编辑器                 ║
║ 🎨 画板                       ║
║ 🧮 计算器                     ║
║ 🎮 贪吃蛇                     ║
╠═══════════════════════════════╣
║ 🖥️ 终端    [立即运行] [固定] ║
║ ⚙️ 设置     [立即运行] [固定] ║
╠═══════════════════════════════╣
║ [注销] [关机] [重启]          ║
╚═══════════════════════════════╝
```

### 4.4 桌面应用

#### 文件管理器
- 双击图标打开
- 支持 FAT32 / EXT2 / EXT4 / RAMFS 分区
- 拖拽移动/复制文件
- 右键菜单：新建/打开/复制/粘贴/删除/重命名/属性

#### 终端模拟器
- 支持命令历史（↑/↓）
- 支持命令补全（Tab）
- 支持管道 `|`、重定向 `>`/`2>`、后台运行 `&`

#### 文本编辑器
- 支持语法高亮
- 行号显示
- 搜索替换 (Ctrl+F / Ctrl+H)

---

## 5. 文件系统

### 5.1 目录结构

```
/
├── bin/           可执行命令 (ls, cp, cat, ...)
├── sbin/          系统管理命令 (ifconfig, mount, ...)
├── home/          用户主目录
│   └── <username>/
├── etc/           系统配置文件
│   ├── passwd    用户账户信息
│   ├── group     用户组信息
│   ├── fstab     文件系统表
│   └── hosts     主机名解析
├── dev/           设备文件 (DEVFS 自动生成)
│   ├── null
│   ├── zero
│   ├── random
│   ├── urandom
│   ├── tty0 ~ tty7
│   └── sd* / hd* (块设备)
├── tmp/           临时文件 (RAMFS)
├── var/           可变数据
│   ├── log/      系统日志
│   └── cache/    缓存文件
├── proc/          进程信息 (PROCFS)
│   ├── self/
│   └── <pid>/
├── sys/           内核对象 (SYSFS)
├── usr/           用户程序
│   ├── bin/
│   └── lib/
└── root/          Sover 主目录
```

### 5.2 挂载点

系统自动挂载分区到指定目录：

```bash
# 查看当前挂载
mount

# 手动挂载 (需要 Admin/Sover)
mount -t fat32 /dev/sda1 /mnt/usb
mount -t ext4 /dev/sda2 /mnt/linux

# 卸载 (需要 Admin/Sover)
umount /mnt/usb
```

### 5.3 VFS 特性

FunsOS 的虚拟文件系统层（VFS）提供以下 POSIX 兼容接口：

```bash
open()   read()   write()   close()   lseek()
stat()   chmod()  chown()   rename()   unlink()
mkdir()  rmdir()  link()    symlink()  readlink()
mount()  umount()  fsync()   truncate()  poll()
```

**高级特性**：

| 特性 | 命令/接口 | 状态 |
|------|-----------|------|
| 文件锁 | `flock(2)` | ✅ 可用 |
| 扩展属性 | `getxattr(2)`/`setxattr(2)` | ✅ 可用 |
| 目录项缓存 | dentry cache (LRU) | ✅ 自动启用 |
| 块缓存 | page cache | ✅ 自动启用 |
| 原子写入 | `vfs_atomic_write()` | ✅ 可用 |
| 文件校验和 | `vfs_checksum_compute()` | ⚠️ CRC32/MD5 可用 |
| 快照 | `vfs_snapshot_create()` | ⚠️ 框架已就绪 |

### 5.4 EXT4 日志

EXT4 文件系统支持 Journal replay，保证断电后文件系统一致性：

```
写操作流程:
  1. 日志写入 (Journal) → 2. 数据写入 (Filesystem) → 3. Commit → 4. 标记完成

断电恢复:
  mount 时自动检测日志状态，对未完成的事务执行 replay/rollback
```

### 5.5 文件压缩

```bash
# 压缩文件 (部分实现)
vfs_compress /path/to/file algorithm

# 解压文件 (部分实现)
vfs_decompress /path/to/file
```

### 5.6 配额管理

管理员可为用户设置磁盘配额：

```bash
# 设置配额
quota_set /home/user --block-soft=1000 --block-hard=2000

# 查看配额
quota_get /home/user

# 检查配额 (写入时自动触发)
quota_check /home/user --blocks=100
```

---

## 6. 网络配置

### 6.1 自动配置 (DHCP)

系统启动时自动通过 DHCP 获取 IP 地址：

```
[启动] → DHCP Discover → DHCP Offer → DHCP Request → DHCP ACK
       → IP/Gateway/DNS 配置完成
```

### 6.2 手动配置

```bash
# 查看网络接口
ifconfig
# eth0:  IP=192.168.1.100  Mask=255.255.255.0  Gateway=192.168.1.1

# 配置 IP 地址 (需要 Admin)
ifconfig eth0 192.168.1.100 netmask 255.255.255.0

# 配置网关 (需要 Admin)
route add default gw 192.168.1.1

# 查看路由表
route -n

# 配置 DNS (需要 Admin)
dns_add 8.8.8.8
dns_add 114.114.114.114
```

### 6.3 防火墙规则

FunsOS 防火墙基于 Netfilter 五链架构：

```
数据包流向:
  [入站]  PREROUTING → ROUTING → LOCAL_IN  → [应用]
  [转发]  PREROUTING → ROUTING → FORWARD → POSTROUTING → [出站]
  [出站]  LOCAL_OUT → POSTROUTING → [出站]
```

**管理防火墙**：

```bash
# 查看防火墙状态
fw_stat

# 查看连接跟踪表
conntrack -L

# 启用/禁用防火墙
fw_enable
fw_disable

# 添加 NAT 规则 (需要 Admin)
# SNAT (源地址转换):
fw_nat_add --type=snat --src=192.168.0.0/16 --to=10.0.0.1

# DNAT (目标地址转换):
fw_nat_add --type=dnat --dst=10.0.0.5 --to=192.168.1.100

# Masquerade (出站接口伪装):
fw_nat_add --type=masquerade --out-iface=eth0

# 清空 NAT 规则
fw_nat_flush
```

### 6.4 网络诊断工具

```bash
# Ping (ICMP)
ping 192.168.1.1
ping -c 4 8.8.8.8

# 查看端口
netstat -tuln

# 查看连接
netstat -an

# 抓包 (需要 Admin)
tcpdump -i eth0
tcpdump -i eth0 port 80

# HTTP 客户端
wget http://example.com/file
wget -O output.html http://example.com

# TFTP 文件传输
tftp -g -r file.txt 192.168.1.1
```

---

## 7. 命令行工具

### 7.1 文件操作

| 命令 | 用途 | 示例 |
|------|------|------|
| `ls` | 列出目录 | `ls -la /home` |
| `cd` | 切换目录 | `cd /var/log` |
| `pwd` | 显示当前目录 | `pwd` |
| `mkdir` | 创建目录 | `mkdir -p /home/user/project/src` |
| `rmdir` | 删除空目录 | `rmdir /tmp/emptydir` |
| `touch` | 创建空文件 | `touch README.txt` |
| `cp` | 复制文件/目录 | `cp -r /src /dst` |
| `mv` | 移动/重命名 | `mv oldname newname` |
| `rm` | 删除文件 | `rm -rf /tmp/cache/*` |
| `cat` | 显示文件内容 | `cat /etc/hosts` |
| `head` | 显示文件开头 | `head -n 20 file.txt` |
| `tail` | 显示文件结尾 | `tail -f /var/log/syslog` |
| `grep` | 文本搜索 | `grep -r "error" /var/log/` |
| `wc` | 统计行/词/字符 | `wc -l access.log` |

### 7.2 系统信息

| 命令 | 用途 | 示例 |
|------|------|------|
| `uname` | 系统信息 | `uname -a` |
| `hostname` | 主机名 | `hostname myhost` |
| `uptime` | 运行时间 | `uptime` |
| `date` | 日期时间 | `date "+%Y-%m-%d %H:%M:%S"` |
| `df` | 磁盘使用 | `df -h` |
| `free` | 内存使用 | `free -m` |
| `top` | 进程监控 | `top`（按 q 退出） |
| `ps` | 进程列表 | `ps aux` |
| `kill` | 终止进程 | `kill -9 1234` |
| `dmesg` | 内核日志 | `dmesg \| tail -50` |

### 7.3 网络命令

| 命令 | 用途 | 示例 |
|------|------|------|
| `ifconfig` | 接口配置 | `ifconfig eth0` |
| `route` | 路由表 | `route -n` |
| `ping` | 连通性测试 | `ping -c 3 8.8.8.8` |
| `netstat` | 网络统计 | `netstat -tuln` |
| `tcpdump` | 抓包 | `tcpdump -i eth0` |
| `wget` | 下载文件 | `wget http://example.com/file` |
| `curl` | HTTP 请求 | `curl http://example.com/api` |
| `dns_lookup` | DNS 查询 | `dns_lookup example.com` |

### 7.4 Shell 特性

FunsOS 内置 Shell 支持：

```bash
# 管道
cat /var/log/syslog | grep error | head -n 10

# 重定向
command > output.txt    # 标准输出重定向
command 2> errors.txt   # 标准错误重定向
command &> all.txt      # 两者合并

# 命令序列
cd /tmp && ls && df -h

# 后台任务
long_running_task &
jobs              # 查看后台任务
fg %1            # 切回前台

# 命令历史
history          # 查看历史命令
Ctrl+R           # 搜索历史

# 命令补全
ls /ho[Tab]     # 自动补全 /home/
```

---

## 8. 系统服务

### 8.1 服务管理

```bash
# 查看所有服务
service -l

# 启动服务
service <name> start

# 停止服务
service <name> stop

# 重启服务
service <name> restart

# 查看服务状态
service <name> status
```

### 8.2 内置服务

| 服务 | 功能 | 状态 |
|------|------|------|
| `login_service` | 用户登录认证 | ✅ 运行中 |
| `desktop_service` | 桌面环境管理 | ✅ 运行中 |
| `notification_service` | 系统通知 | ✅ 运行中 |
| `syslogd` | 系统日志收集 | ✅ 可用 |
| `cron` | 定时任务 | ⚠️ 框架已就绪 |
| `sshd` | SSH 远程登录 | ❌ 待实现 |

### 8.3 日志管理

```bash
# 查看系统日志
cat /var/log/syslog
dmesg | tail -100

# 查看认证日志
cat /var/log/auth.log

# 查看应用程序日志
cat /var/log/app.log

# 旋转日志 (需要 Admin)
logrotate -f /etc/logrotate.conf
```

---

## 9. 开发者接口

### 9.1 系统调用

FunsOS 提供 200+ 系统调用，通过 `int 0x80` 触发（也支持 `sysenter` 快速路径）：

```c
// 系统调用号定义 (kernel/syscall.h)
#define SYS_exit          1
#define SYS_fork          2
#define SYS_read          3
#define SYS_write         4
#define SYS_open          5
#define SYS_close         6
#define SYS_waitpid       7
#define SYS_creat         8
#define SYS_link          9
#define SYS_unlink       10
#define SYS_execve       11
#define SYS_chdir        12
#define SYS_time         13
// ... 更多见 kernel/syscall.h
```

**调用示例**（在用户态程序中）：

```c
#include <sys/syscall.h>
#include <unistd.h>

// 使用 libc 封装
ssize_t bytes = write(STDOUT_FILENO, "Hello FunsOS\n", 13);

// 或直接 syscall
asm volatile ("int $0x80"
    : "=a"(ret)
    : "a"(SYS_write), "b"(STDOUT_FILENO), "c"(buf), "d"(len));
```

### 9.2 Socket API

FunsOS 网络栈兼容 BSD Socket 接口：

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// 创建 TCP 套接字
int sock = socket(AF_INET, SOCK_STREAM, 0);

// 连接到服务器
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(80);
addr.sin_addr.s_addr = inet_addr("93.184.216.34");
connect(sock, (struct sockaddr *)&addr, sizeof(addr));

// 发送数据
const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
send(sock, req, strlen(req), 0);

// 接收响应
char buf[4096];
ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
buf[n] = '\0';
printf("%s", buf);

// 关闭
close(sock);
```

**支持的 Socket 类型**：

| 类型 | 用途 |
|------|------|
| `SOCK_STREAM` | TCP 字节流 |
| `SOCK_DGRAM` | UDP 数据报 |
| `SOCK_RAW` | 原始数据包 (需要 Admin) |

### 9.3 进程与线程

```c
#include <sched.h>
#include <pthread.h>

// 创建线程
pthread_t tid;
pthread_create(&tid, NULL, thread_func, arg);
pthread_join(tid, NULL);

// 进程调度策略
// 查看 /proc/<pid>/sched 或使用 sys_sched_setparam()

// 设置调度策略
sched_set_policy(PROCESS_CFS);    // 完全公平调度
sched_set_policy(PROCESS_DEADLINE, runtime, period, deadline);
sched_set_policy(PROCESS_MLFQ);  // 多级反馈队列
```

### 9.4 内存映射

```c
#include <sys/mman.h>

// 匿名内存映射
void *buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

// 文件内存映射
int fd = open("data.bin", O_RDONLY);
void *data = mmap(NULL, filesize, PROT_READ,
                  MAP_PRIVATE, fd, 0);

// 解除映射
munmap(buf, 4096);
```

### 9.5 进程间通信

**管道**：

```c
int pipefd[2];
pipe(pipefd);
if (fork() == 0) {
    close(pipefd[0]);
    write(pipefd[1], "hello", 5);
    exit(0);
}
close(pipefd[1]);
read(pipefd[0], buf, 5);
```

**共享内存**：

```c
// 创建共享内存
int shmid = shmget(IPC_KEY, 4096, IPC_CREAT | 0666);
void *shm = shmat(shmid, NULL, 0);

// 附加到进程
void *shm = shmat(shmid, NULL, 0);

// 分离
shmdt(shm);

// 删除
shmctl(shmid, IPC_RMID, NULL);
```

### 9.6 信号处理

```c
#include <signal.h>

void sigint_handler(int sig) {
    printf("Caught SIGINT (%d)\n", sig);
    // 清理并退出
}

signal(SIGINT, sigint_handler);
signal(SIGSEGV, SIG_DFL);  // 恢复默认处理

// 发送信号
kill(pid, SIGTERM);

// 暂停等待信号
pause();
```

---

## 10. 故障排除

### 10.1 常见启动问题

**Q: 系统启动后卡在 "Loading..."**
```
可能原因: 磁盘镜像未正确加载
解决: 
  1. 确认 os.img 路径正确
  2. 检查 QEMU 参数: -drive format=raw,file=build/os.img
  3. 尝试增加内存: -m 256
```

**Q: 图形模式无法切换**
```
可能原因: VESA BIOS 不支持请求的分辨率
解决:
  1. 系统自动回退到 800x600 模式
  2. 或在 VBE 兼容模式下运行
```

### 10.2 网络问题

**Q: DHCP 获取 IP 失败**
```
可能原因: 网络驱动未加载或无 DHCP 服务器
解决:
  1. ifconfig 查看接口状态
  2. 手动配置 IP: ifconfig eth0 192.168.1.100
  3. 检查 QEMU 网络配置 (-net nic,model=rtl8139)
```

**Q: 无法 Ping 通网关**
```
可能原因: 网关未配置 / 防火墙阻断
解决:
  1. route -n 查看路由表
  2. ping 本地网关地址
  3. fw_stat 检查防火墙规则
  4. 尝试禁用防火墙: fw_disable
```

### 10.3 文件系统问题

**Q: EXT4 分区只读**
```
可能原因: 文件系统损坏 / 权限不足
解决:
  1. 以 Admin/Sover 身份操作
  2. 检查 /etc/fstab 配置
  3. 使用 fsck 检查文件系统
```

**Q: mount 失败**
```
可能原因: 未知文件系统类型 / 设备不存在
解决:
  1. ls /dev 查看可用设备
  2. mount -t auto /dev/sda1 /mnt 测试
  3. dmesg | tail 查看内核日志
```

### 10.4 性能问题

**Q: 系统响应缓慢**
```
可能原因: 内存不足 / 进程过多
解决:
  1. top 查看 CPU/内存使用
  2. free -m 查看内存
  3. kill 终止异常进程
  4. 增加 QEMU 内存: -m 256 或 -m 512
```

**Q: 网络带宽低**
```
可能原因: MTU 不匹配 / TCP 拥塞控制
解决:
  1. 检查网络接口 MTU: ifconfig eth0
  2. TCP 调优: sysctl -w net.ipv4.tcp_window_scaling=1
```

### 10.5 获取帮助

| 渠道 | 说明 |
|------|------|
| 内核日志 | `dmesg` 查看启动消息 |
| 系统日志 | `cat /var/log/syslog` |
| 调试模式 | `mingw32-make debug` + GDB |
| 源码文档 | `kernel/`、`net/`、`fs/` 目录下的 `README.md` |
| 问题追踪 | 查看 `docs/` 目录下的 ROADMAP.md 和 FEATURE_MATRIX.md |

---

## 附录 A: 键盘快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+Del` | 打开任务管理器 |
| `Ctrl+Alt+F1~F7` | 切换虚拟终端 |
| `Ctrl+C` | 中断当前命令 |
| `Ctrl+Z` | 挂起当前进程 |
| `Ctrl+D` | EOF / 退出 Shell |
| `Ctrl+L` | 清屏 |
| `Ctrl+S` | 暂停输出 |
| `Ctrl+Q` | 恢复输出 |
| `Alt+Tab` | 切换窗口 |
| `Alt+F4` | 关闭窗口 |

---

## 附录 B: 系统限制

| 项目 | 限制值 |
|------|---------|
| 最大进程数 | 1024 |
| 最大打开文件描述符/进程 | 1024 |
| 最大用户数 | 65535 |
| 最大线程数/进程 | 256 |
| 文件系统最大文件大小 | 2 GB (FAT32) / 16 TB (EXT4) |
| 内存地址空间 | 4 GB (PAE 可扩展) |

---

*本文档最后更新于 FunsOS v0.8.2*
