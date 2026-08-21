# FunsOS / FunsCore — 完善路线图 (Brainstorm v1)

> 本文档对应 `kernel/* + fs/* + net/* + drivers/*` 四个区域，遵循用户已确认的策略：
> **方向 = 把已有功能补完做厚**；**不重命名**；**不重写**。

---

## 0. 项目现状速记 (基线)

| 指标 | 数值 |
|------|------|
| 仓库总文件数 | **1673** |
| `kernel/` C/asm | **326** |
| `fs/` C/头 | **~40** |
| `net/` C/头 | **~56** |
| `drivers/` C/头 (含子目录) | **~80** |
| 基线构建 | ✅ `os.img` ≈ 26 MB，能完整跑通 `mingw32-make -j4` |
| `kernel.elf` | 2.79 MB |
| README 与代码一致性 | ⚠️ README 浮夸（宣传 200+ syscall、11 文件系统、10 NIC），但很多子系统是真实代码而非空壳；同时 `printf("%llu",...)` 大量裸用，依赖 freestanding 平台 ABI，与 PE 输出格式不冲突 |

### 现状里**真正薄弱**的位置（已逐项抽样确认）

| # | 位置 | 现状 | 问题 |
|---|------|------|------|
| **B1** | `boot/linker.ld` | `ENTRY(_start)`, `. = 0x100000`, 但 `-Wl,--oformat=pei-i386` | PE 格式的默认 image base = 0x10000，链接器反复报 `section below image base`。链接输出非纯二进制可执行 ELF，等于把内核编成了 Windows .EXE 格式，需要 objcopy 才得到 `kernel.bin`。 |
| **B2** | 大量 `printf("%llu", u64)` | 见 `kernel/sched.c:570` 等多处 | freestanding 下大多数 toolchain 默认 `long long` 与 `unsigned long` 都按 64-bit 看待，**通常可工作**，但 `-Wformat` 不会触发。在 -m32 下混用 `unsigned long long` 会有额外开销，且部分自定义 fmt 实现未实现 `%llu`。 |
| **B3** | README 声称的子系统数 vs 实际 | 比如声明"BTRFS 快照"，但 `fs/btrfs.c` 只有 ~12KB，且 TODO 不少 | 用户最容易被"读 README 以为能做 → 实际跑挂"的落差伤到，需要把"做什么 / 不做什么"写清楚。 |
| **B4** | `kernel/shell.c` | 840 KB，单文件 | 内建 shell 是项目里最大的单一 C 文件，命令调度/解析/补全/历史都在里面。Shell 不算内核核心，但 README 当内核模块列出。 |
| **B5** | 头文件格式串不严格 | 部分驱动直接 `printf("%d", 64bit 值)` | 在 -m32 下可能丢精度。需要统一驱动日志格式。 |
| **B6** | `apps/*.c` 中 `*_app.c` 与同名无后缀 | 重复文件 + Makefile 合并到 `apps/*_app.c` 与 `apps/init.c` | 双份维护风险，疑似 git 管理漂移。 |
| **B7** | 部分驱动文件声明 `__attribute__((unused))` 后 `static` 函数被裁掉 | 见 `kernel/sched.c:1068` | 编译期消除，对运行期无害，但说明 API 半成品。 |

---

## 1. 路线图总览（按子系统）

> 每条都满足：**(a) 明确交付物**、**(b) 不破坏 ABI/syscall 号**、**(c) build 可通过**。

| 区域 | 模块 | 现状 | 完善动作 | 优先级 |
|------|------|------|---------|--------|
| **构建** | `boot/linker.ld` + Makefile | 输出 PE 导致告警 | 把链接脚本切到纯 ELF (`-Wl,--oformat=elf32-i386`)，改用 `objcopy -O binary` 已有的链路；或加 `-Wl,--image-base=0x100000` 抑制告警 | P0 |
| **构建** | 编译告警收敛 | `unused-variable`、`comparison is always false` 散落各处 | 给 freestanding 工具链设 `-Wno-type-limits`（自实现 wchar.h 限制了 char 范围），其余保留 `-Werror` 风险评估 | P1 |
| **内核核心** | `kernel/sched.c` | 4 种策略 + CFS + Deadline + 优先级继承 + CPU 亲和性 + 能量感知 | 完成 `cgroup` 配额联动；让 `sched_set_policy_cfs()` 与 RT/MLFQ 真正互斥；deadline bandwidth 校验 | P1 |
| **内核核心** | `kernel/process.c`、`signal.c`、`vmm.c` | 真有实现但 TODO 散落 (12/2/2 处) | 收尾 TODO、补 `coredump`/`kthread` 文档、API 注释统一 | P1 |
| **内核核心** | `kernel/syscall_impl.c` | 1100+ 行，1099 行未读 | 验证所有 `SYS_*` 号与 `syscall.c` 一致；补齐 errno 返回路径 | P1 |
| **文件系统** | `fs/vfs.c` / `fs/vfs_ext.c` | 45KB / 103KB 巨大 | 拆出独立的 `vfs_cache.c` / `vfs_acl.c`，强化目录项缓存统计 (命中率)；新增 O_PATH / O_TMPFILE | P1 |
| **文件系统** | `fs/btrfs.c`、`fs/xfs.c` | 数千行 | 把 read 实现做压力测试，挂入 `mkfs.funs_btrfs` 工具校验路径 | P2 |
| **文件系统** | `fs/ext4.c` + `ext4_journal.c` | 39KB+7KB | 完成 journal replay 单元测试，commit/fsync 链路要保住 | P2 |
| **文件系统** | `fs/fuse.c` | 19KB | 加上 mmap writeback (FUSE_WRITE + FUSE_COPY_FILE_RANGE) | P2 |
| **网络** | `net/ip.c`、`net/ipv6.c` | 33KB / 39KB | 校验 IPv4 选项 / IPv6 扩展头解析；IPv6 增加 Path MTU；jumbogram | P1 |
| **网络** | `net/tcp.c` + `tcp_state.c` + `tcp_congestion.c` | 76KB + 17KB | 把 Reno/Cubic/BBR 接入状态机；SACK 选项解析 | P1 |
| **网络** | `net/socket.c` | 41KB | 收尾 `sendfile`/`splice`/`recvmmsg`；BSD `SO_TIMESTAMP` 体系 | P1 |
| **网络** | `net/ssl_tls.c` | 32KB | 真 TLS 1.2 握手 (RSA+AES-GCM)；自实现证书链校验 | P3 |
| **驱动** | NIC 全家 (e1000/rtl8139/i219/i225/picnet/ne2000/ixgbe/virtio_net/b57/dm9000/connectx3) | 多个 stub 注释 | 选 3 款做 loopback 集成测试；统一 `net_driver.h` ops 接口 | P2 |
| **驱动** | `drivers/usb/xhci_enhanced.c` 33KB | 真的有增强 | 接通 USB-HID 热插拔事件到 input 子系统 | P2 |
| **驱动** | `drivers/sata_advanced.c` 34KB | AHCI 扩展 | 加 NCQ 命令提交/完成队列的 debugfs-style 状态输出 | P3 |
| **驱动** | `drivers/gpu/{amdgpu,i915,drm}.c` | 87KB+24KB+21KB | DRM 节点最小化 (`/dev/dri/card0`)；modeset ioctl 子集 | P3 |
| **驱动** | `drivers/char/{tablet,touchpad,sensors,shmdev,vserial}` | 6 文件 | 把 `/dev/tablet`、`/dev/touchpad` 走 input 子系统；`/dev/shm` POSIX 化 | P2 |
| **交叉** | syslog / klog / logrotate | 已实现 | 串联：用户态 syslog() → 内核环形缓冲 → logrotate 落盘 → syslogd 解析 | P2 |

---

## 2. "补厚" 的具体实施建议（按优先级）

### P0 — 立刻可做、风险最低、收益最高

1. **链接脚本与 PE 输出修正**  
   - `boot/linker.ld` 顶部 `OUTPUT_FORMAT(elf32-i386)`  
   - `Makefile` `LDFLAGS` 把 `--oformat=pei-i386` 换成 `-melf_i386`，改用纯 ELF→binary 的路径  
   - `kernel.elf` 不再误输出 .EXE 头部（虽然 objcopy 已经能剥除）  
   - 受益：消除 7 行 `section below image base` 警告，未来加 DWARF debug section 不再需要绕过

2. **驱动的格式串统一**  
   - 提供 `lib/kprintf.h`：用 `PRIu64`/`PRIx64` 宏，替代裸 `"%llu"`  
   - 一次性 sed 替换 `kernel/*.c`、`fs/*.c`、`net/*.c`、`drivers/**/*.c` 内的 `%llu`/`%lld`  
   - 受益：跨 GCC/LLANG、跨 32/64 平台一致

3. **README 与代码事实表**  
   - `docs/FEATURE_MATRIX.md`：每条 README 特性 → 真实代码路径 → 完成度（✅/⚠️/❌）  
   - 受益：避免后续 contributor 走错方向

### P1 — 子系统级健壮性提升（按区域分批做）

#### 2.1 内核核心
- `sched.c`：`sched_set_policy_cfs` 应清空旧策略位 (`proc->sched_policy = PROCESS_CFS`) 而不是 OR，避免策略叠加；`sched_dl_bandwidth_check()` 真正检查总带宽 ≤ 100%
- `process.c`：补上 `do_exit()` 路径对 vfork 父进程的特殊处理
- `signal.c`：补 `SA_RESTORER` + sigreturn trampoline 正确性
- `vmm.c`：把 cow + anon page + shmem 串起来形成统一 page fault 处理路径
- `syscall_impl.c`：每个 `SYS_*` 函数前用 `if (current_proc == NULL) return -ESRCH;` 守卫

#### 2.2 文件系统
- VFS：`vfs_advanced.c` 中的 `fallocate`、`sync_file_range`、`copy_file_range` 实现闭环
- dentry cache：增加 LRU 淘汰统计 (`dcache.c` 内嵌计数器)
- page cache：在 `readahead.c` 实现顺序预读 + 窗口调整
- ext4 journal：在断电模拟测试中 replay 5~10 个事务
- BTRFS：实现 COW 单节点读写路径（已有 → 验证）
- FUSE：补 `FUSE_RELEASE` + `FUSE_READDIR` 协议字段

#### 2.3 网络
- TCP：SACK 选项 (`tcp_state.c`)，DSACK，Cubic 拥塞状态机 (`tcp_congestion.c`)  
- IP：分片重组（`ip.c`），TTL=0/1 时返回 ICMP  
- IPv6：PMTUD (Path MTU Discovery), 邻居缓存老化（已有 → 验证）
- Socket：`recvmmsg`/`sendmmsg`，`SO_TIMESTAMPNS`, `TCP_NODELAY`
- Firewall (`fw.c`)：把 netfilter 五链 INPUT/OUTPUT/FORWARD/PREROUTING/POSTROUTING 真正实现
- DHCP (`dhcp.c`)：补 RENEW/REBIND 状态转换
- DNS (`dns.c`)：补 EDNS0 + TCP fallback (RFC 7766)

#### 2.4 驱动
- NIC：选 e1000 / rtl8139 / virtio_net 做 loopback 自测试
- USB：USB-HID 热插拔事件 → input 子系统
- SATA：AHCIPCIe 重置 + NCQ 命令提交
- 字符设备：`/dev/urandom` 用熵池（`krng.c`）作为后端

### P2 — 子系统外延（每条独立 PR）

1. **ACPI 真实化** — `acpi.c` / `acpi_sleep.c`：解析 MADT、HPET、FADT；不只调口
2. **PCIe 完整化** — `pcie.c` 22KB：MSI-X 中断、PCIe hot-plug
3. **KVM 真实化** — `kvm.c` 24KB：实现 vCPU 创建 + 一段小程序 VM-exit 演示
4. **AC97/HDAudio 真实化** — 跑出正弦波
5. **VFS 加密层** — `crypto.c` 接入：`fsync(f)` 后做 AES-GCM integrity tag
6. **配额系统联动** — `quota.c` ↔ `cgroup.c` ↔ `sysacct.c`

### P3 — 长期（需持续打磨）

1. **TLS 1.2 / 1.3** 真正握手
2. **DRM/KMS** 用户态 modeset
3. **包管理器** (`pkgmgr.c`)：仓库元数据格式稳定化
4. **国际化 (i18n)**：UTF-8 → 当前 locale 输出
5. **FunDB** 真正支持 JOIN/聚合

---

## 3. 这次会话的目标 (建议)

因为单次会话不宜把 P0+P1+P2 全部做完，**建议这次只交付 P0 + P1 的子集**，具体三件可量化的事：

1. **构建**：消除 7 行 linker 警告，sed 全量替换 `%llu` → `PRIu64`
2. **路线图落地文档**：`docs/FEATURE_MATRIX.md`（≥ 80 条特性矩阵）
3. **内核 + FS + 网络的"明显薄弱"修复**（按上表 B1/B5/B6 三处）：
   - 修 `linker.ld` 的 PE 警告
   - 收尾 `sched.c` 的策略叠加逻辑
   - 补 `vfs.c` 的 dentry cache 命中率统计
   - 补 `tcp_state.c` 的 SACK 选项解析入口
   - 把 `apps/` 重复的 `*_app.c` 与无后缀文件去重

> 以上任何超出范围的改动都需要先和用户对齐。

---

## 4. 不要做的事（避免无意义消耗）

| ❌ 事项 | 原因 |
|--------|------|
| 把 README 里所有"已实现"改成"未实现" | 多数模块**有真实代码**，反而应当**承认完成度**并补到真正完成 |
| 全面重命名 FunsOS/FunsCore → 其他 | 用户已确认 `rename_skip` |
| 把 1673 文件拆 git 子模块 | 单次会话内不可行 |
| 重新设计 syscall ABI | 已稳定，破坏 ABI 会让 SDK/示例全部失效 |
| 引入 Rust/C++ 重写 | 与现有 C/ASM 工程不兼容 |