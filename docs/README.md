# docs/ — 项目文档

> v0.9 起，`docs/` 目录集中存放重构与架构说明。**单文件级别**的说明放在该文件自身的头部注释中;
> **跨模块**的说明放在这里。

## 文档索引

| 文档 | 内容 | 何时阅读 |
|------|------|---------|
| `NAMING_CONVENTIONS.md` | 函数前缀/类型后缀/常量风格/头文件分组 | 写新代码前 |
| `../os/README.md` | os/ 目录(用户态组件)分层与依赖 | 改 os/* 前 |
| `../os/apps/README.md` | 桌面 GUI 应用主路径 | 加新桌面应用前 |
| `../os/desktop/README.md` | 桌面环境(壁纸/任务栏/开始菜单) | 改桌面组件前 |
| `../os/services/README.md` | 后台服务(剪贴板/登录/通知/电源) | 加新服务前 |
| `../apps/README.md` | 早期 CLI 应用与废弃 GUI 应用清单 | 清理 apps/* 前 |
| `../userland/README.md` | 用户态独立程序清单与约束 | 改 userland/* 前 |
| `../gui/README.md` | gui/ 基础图形子系统 | 改 gui/* 前 |
| `../renderer/README.md` | FunRender 独立渲染引擎(43 KB) | 改 renderer/* 前 |

## 重构路线图(2026-08 起)

```
v0.9 (当前)    ── 真正用户态进程隔离 + fd 族(eventfd/timerfd/signalfd)
                    ├─ eventfd/timerfd/signalfd 内核对象实现
                    ├─ lib/tinyevloop epoll-lite 轮询集
                    ├─ xattr (4 个 namespace) + quota (soft/hard/grace)
                    ├─ io_uring 异步 I/O ring
                    ├─ ICMPv6 + PMTUD + TCP SACK helper
                    ├─ SA_RESTORER + signalfd
                    ├─ lib/rbtree (Cormen) + CFS
                    └─ 9 个新 shell 命令 (xattr/acpi/signal/rbtree/io/eventfd/timerfd/signalfd/evloop)
v1.0 (目标)    ── GUI 桌面完善 + 用户态 ELF 进程
```