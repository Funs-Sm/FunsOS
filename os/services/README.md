# os/services/ — 系统服务(后台独立线程)

> 每个服务都是内核线程,有标准生命周期:init → run loop → shutdown。
> 由 `kernel/service_registry.c` 统一调度(每个服务在 `kernel/service_registry.h` 顶端注释里有引用)。

## 服务清单

| 服务 | 源文件 | 职责 | 依赖 |
|------|--------|------|------|
| 剪贴板 | `clipboard_service.c/h` | 文本 + 图像剪贴板,应用间通过 IPC 共享 | `kernel/ipc_shm.c` |
| 登录 | `login_service.c/h` | 用户认证 + 会话切换 | `kernel/user.c`、`kernel/user_persist.c` |
| 通知 | `notification.c/h` | 系统通知气泡(右上角弹出 + 消息中心) | `gui/window.c`、`gui/wm.c` |
| 电源 | `power_service.c/h` | ACPI 电源管理、关机/重启接口 | `kernel/acpi.c`、`kernel/acpi_sleep.c` |

## 通用约定

- 服务线程入口:`void *<service>_thread(void *arg)`,由 `service_registry` 创建
- 服务间通信:统一通过 `kernel/ipc_msg.c`(消息队列);**不要**用共享内存直连
- 配置:每个服务在 `/etc/<service>.conf` 存自己的设置,通过 `kernel/config.c` 读写
- 错误日志:通过 `kernel/syslog.c` 而非 printf

## 重构注意

- `notification_service` 当前未在 `kernel/service_registry.c` 注册表中(应该注册),**Phase B** 修复
- `power_service` 的关机调用当前直接走 `kernel/acpi.c`,应改为 IOCTL 风格的系统调用接口