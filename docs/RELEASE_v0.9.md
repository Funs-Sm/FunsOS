# FunsOS v0.9 Release Notes

> **版本跳跃**: FunsCore 内核从 v0.8.7 直接跃升至 v0.9，跳过了 v0.8.8/v0.8.9。本次发布包含两个月累计的内核子系统工作，以及之前未能在 v0.8.x 系列中落地的安全加固。

---

## 发布亮点

| 类别 | 新增/改进内容 |
|------|--------------|
| **文件系统** | xattr（4个namespace）+ quota（soft/hard/grace）+ io_uring 异步I/O + path_hash + inode cache hit-rate 改进 |
| **网络栈** | ICMPv6 + PMTUD + TCP SACK 公开helper + SO_TIMESTAMP/NS/ING |
| **调度器** | lib/rbtree（Cormen算法）+ CFS 接入 SACK-aware 重传 |
| **信号子系统** | SA_RESTORER标志 + 默认sigreturn兜底 + signalfd |
| **fd 族** | eventfd / timerfd / signalfd + lib/tinyevloop 轮询集 |
| **Shell** | 10个新 `cmd_*` 命令（xattr/acpi/signal/rbtree/io/eventfd/timerfd/signalfd/evloop/pt）|
| **系统调用** | 新增多个 syscall 并完善 getsockopt |

---

## 详细变更

### 文件系统

- **xattr 扩展属性** — `fs/xattr.c` 实现 4 个 namespace（user/trusted/security/system），提供 `xattr -l/-g/-s/-r FILE` 四个操作，支持 ACL、安全标签等场景。
- **磁盘配额** — `kernel/quota.c` 实现 soft/hard limit + grace period，配额数据通过 `quota_db_sync()` 持久化到 FunDB。
- **io_uring** — 异步I/O ring，支持提交/完成队列统计，新增 `cmd_io` 展示提交/完成/字节数。
- **path hash** — 新增 `path_hash` 命令，统计路径哈希表碰撞情况。
- **inode cache** — `cmd_icache` 展示 inode cache hit/miss 比率，辅助性能调优。

### 网络栈

- **ICMPv6** — 完整的 IPv6 邻居发现与 Ping 支持。
- **路径MTU发现（PMTUD）** — 避免分片并自动协商最佳MTU。
- **TCP SACK** — Selective Acknowledgement 公开helper函数，改善高延迟网络吞吐。
- **Socket选项** — `SO_TIMESTAMP` / `SO_TIMESTAMPNS` / `SO_TIMESTAMPING` 完善，支持 getsockopt 查询时间戳。

### 调度器

- **lib/rbtree** — 移植 Cormen 算法实现的红黑树，作为 CFS 的vruntime管理基础。
- **CFS接入SACK-aware重传** — 拥塞控制与调度协同优化。

### 信号子系统

- **SA_RESTORER** — 新增 `SA_RESTORER` 标志，兼容 legacy sigaction 用法。
- **sigreturn兜底** — 无自定义handler时使用默认 `sigreturn`。
- **signalfd** — 信号转fd接口，进程可用 `poll/read` 等待信号事件。

### fd 族

- **eventfd** — 进程间事件通知计数器，支持 `EFD_SEMAPHORE` 语义（递减而非清零）。
- **timerfd** — tick驱动定时器，一次性与周期模式，由PIT中断 `timer_handler()` 中的 `timerfd_tick()` 推进。
- **signalfd** — 全局广播式信号转发，mask 过滤，pending位图管理。
- **tinyevloop** — 极简 epoll-lite，封装三个fd的 `is_ready()` 判断与回调分发。

### Shell 命令

| 命令 | 功能 |
|------|------|
| `xattr` | 扩展属性增删改查（`-l/-g/-s/-r`） |
| `acpi` | ACPI信息（stub升级为真实查询） |
| `signal` | 信号处理统计 |
| `rbtree` | 红黑树统计（adds/dels/searches/hits） |
| `io` | io_uring 提交/完成/字节数 |
| `eventfd` | eventfd创建/读/写/唤醒统计 |
| `timerfd` | timerfd创建/到期/唤醒统计 |
| `signalfd` | signalfd创建/转发/丢弃统计 |
| `evloop` | tinyevloop 注册/分发统计 |
| `pt` | procfs线程信息 |

### 其他改进

- `kernel/main.c` 新增 `io_uring_reset_stats()` 初始化调用。
- `kernel/timer.c` 的 `timer_handler()` 中调用 `timerfd_tick(ticks)`，使 timerfd 真正随PIT tick触发。
- `kernel/quota.c` 的 `quota_sync()` 接入 `quota_db_sync()` 持久化。
- `kernel/shell.c` 接入 `setquota` / `repquota` / `path_hash` 三个命令。
- `kernel/cmd_procinfo.c` 追加完整的 `cmd_xattr` 实现。

---

## 资产

| 资产 | 说明 |
|------|------|
| `os.img` | 引导磁盘镜像：1 MiB BIOS区域 + boot/loader + kernel.elf + tarfs段 |
| `kernel.bin` | flat-binary形式内核，方便引导装载机直接顺序写盘 |
| `kernel.elf` | 含调试符号的ELF（pei-i386, image-base 0x100000）|
| `kernel.map` | 完整符号表，便于panic时还原调用栈 |
| `source.tar.gz` | git archive出来的源码（不含build产物） |

---

## 验证方法（QEMU）

```bash
qemu-system-i386 -machine accel=tcg -m 256 \
  -drive format=raw,file=build/os.img \
  -serial stdio
```

启动后输入 `help` 列出所有命令，`dmesg` 查看内核日志，`rbtree` 查看红黑树统计，`eventfd` / `timerfd` / `signalfd` 分别查看三个fd子系统的状态。

---

## 已知问题

参见 `docs/ROADMAP.md` 的 v1.0 章节。

---

## 从 v0.8.x 升级

- SDK头文件保持ABI兼容（所有新增syscall均有 `#define SYS_*` 数值）。
- 现有v0.8.x用户态二进制无需重新编译（除非使用本次新增的syscall）。
- 内核版本 `KERNEL_VERSION` 从 `0.8.7` 更新为 `0.9`。

---

**构建**: `mingw32-make all`（Windows）或 `make all`（Linux/macOS）
**运行**: `run.bat`（Windows）或 `bash run.sh`（Linux/macOS）
