# userland/ — 独立用户态程序

> 与 `apps/` 不同,这里的目标是**真正不链入内核**的程序(虽然当前仍静态链入同一镜像,
> 但设计上已经做到只依赖 `sdk/include/` 公共头 + `lib/` C 库)。
> 重构目标(v0.9+):这些程序改为独立 ELF,被 `os/fun_format.c` 加载。

## 文件清单

| 文件 | 用途 | 入口 | 备注 |
|------|------|------|------|
| `init.c` | PID 1 主控 | `user_init()` | 调度启动顺序 |
| `login.c` | 用户登录 | `user_login()` | CLI 模式登录 |
| `ifconfig.c` | 网络配置查看/设置 | `cmd_ifconfig()` | |
| `ping.c` | 网络诊断 | `cmd_ping()` | 发送 ICMP echo |
| `top.c` | 系统监视器 | `cmd_top()` | 周期刷新进程/内存/CPU |
| `sysmon.c` | 系统监控面板 | `app_sysmon_run()` | GUI 风格控制台面板 |
| `dbtool.c` | FunDB 调试工具 | `cmd_dbtool()` | 打开/创建/查询 |
| `fsearch.c` | 文件搜索工具 | `cmd_fsearch()` | 全 VFS 树扫描 |
| `virc.c` | 虚拟 IRC 客户端 | `app_virc_run()` | GUI,基于 socket.c |
| `wget.c` | HTTP 下载工具 | `cmd_wget()` | 基于 http_client.c |
| `audio_player.c` | 音频播放器 | `app_audio_player_run()` | GUI,基于 audio/sound.c |

## 设计约束

1. **每个文件只依赖 `sdk/include/` + `lib/` 公共头** — 不允许 `#include "../kernel/..."` 这种"穿透到内核"的引用。
2. **入口函数命名**:`cmd_<name>()` 表示 CLI 命令;`app_<name>_run()` 表示 GUI 应用。
3. **错误码**:负值 errno(`-EINVAL`、`-ENOSYS` ...),与 Linux 兼容。
4. **持久化**:配置文件在 `/etc/<tool>.conf`。

## 已知 TODO

- `audio_player.c` 当前依赖 `audio/sound.c` 的内核态混音器;v0.9 应通过 IOCTL 系统调用访问。
- `virc.c` 的 IRC 协议解析是简版,只支持基本 PRIVMSG/NOTICE;未来应拆出独立 `protocols/irc.c`。
- `fsearch.c` 全树扫描在大型文件系统上慢;应改用 `procfs` 风格的迭代器。