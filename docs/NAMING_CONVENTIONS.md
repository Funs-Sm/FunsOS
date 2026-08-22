# 命名/接口约定 — FunsCore/FunsOS v0.9

> 本文档汇总项目内的**约定俗成**;违反约定的代码在 Phase D 重构中被标记。
> 约定**优先可读性而非严格执行**:遵循约定的新代码更容易被读者找到。

## 函数前缀

| 前缀 | 模块 | 文件位置 | 例子 |
|------|------|---------|------|
| `kernel_*` | 内核子系统 | `kernel/*.c` | `kernel_main()`,`kernel_vfs_init()` |
| `sys_*` | 系统调用入口 | `kernel/syscall*.c` | `sys_write()`,`sys_open()` |
| `cmd_*` | Shell 内建命令 | `kernel/shell.c` | `cmd_ls()`,`cmd_cat()` |
| `app_*_main` / `app_*_run` | 应用入口 | `kernel/shell.c` + `os/apps/*` | `app_notepad_main()`,`app_calculator_run()` |
| `gui_*` / `widget_*` / `window_*` / `wm_*` | GUI 基础 | `gui/*` | `gui_app_paint()`,`widget_button_draw()` |
| `fr_*` | 渲染引擎公开 API | `renderer/include/fr_*.h` | `fr_widget_create()`,`fr_effect_drop_shadow()` |
| `os_*` | OS 层服务 | `os/services/*` | `os_clipboard_set()`,`os_login_run()` |
| `desktop_*` | 桌面环境 | `os/desktop/*` | `desktop_init()`,`desktop_show_window()` |
| `taskbar_*` / `start_menu_*` | 桌面组件 | `os/desktop/*` | `taskbar_refresh()` |
| `funsos_*` | SDK 公开 API | `sdk/include/funsos_*.h` | `funsos_print()`,`funsos_file_open()` |
| `fs_*` / `vfs_*` | 文件系统 | `fs/*` | `fs_read()`,`vfs_lookup()` |
| `net_*` | 网络子系统 | `net/*` | `net_send()`,`net_socket_open()` |

## 类型命名

| 后缀 | 含义 | 例子 |
|------|------|------|
| `_t` | 类型(struct/typedef) | `process_t`,`widget_t`,`fr_color_t` |
| `_s` | 旧式结构(v0.6 之前) | `process_s` — 不再使用,迁移到 `_t` |
| `_info_t` | 只读信息结构 | `process_info_t`,`memory_info_t` |
| `_state_t` | 运行时状态 | `desktop_state_t`,`login_state_t` |
| `_config_t` | 配置参数 | `fr_drop_shadow_t`,`fr_glow_t` |
| `_entry_t` | 表项 | `app_entry_t`,`vfs_dentry_t` |
| `_ops_t` | 操作回调表 | `vfs_ops_t`,`socket_ops_t` |

## 常量与宏

| 风格 | 含义 | 例子 |
|------|------|------|
| `UPPER_SNAKE_CASE` | 宏定义/常量 | `APP_MAX_COUNT`,`FR_BLEND_SRC_OVER`,`SETTINGS_CAT_DISPLAY` |
| `MOD_PREFIX_*` | 模块常量带模块前缀 | `WINDOW_FLAG_*`(gui/window.h),`FR_BLEND_*`(renderer) |
| `enum` 替代 `#define` | 新代码优先用 enum | `enum app_type_t { APP_TYPE_SYSTEM = 0, ... }` |

## 头文件分组

- 内部模块之间:`#include "<子目录>/<文件>.h"`
- 跨模块边界:用桥接头(`gui/desktop_bridge.h`、`gui/renderer_bridge.h`),**不要**直接 include 内部头
- SDK 头:`sdk/include/funsos_*.h` — 用户态程序唯一允许的 include 路径

## 错误码

- 函数返回 `< 0` 表示错误,正值或 0 表示成功
- 错误值参考 `lib/errno.h`(`-EINVAL`、`-ENOSYS`、`-ENOMEM` ...)

## 当前(2026-08)违反约定的热点

1. `kernel/shell.c` 中 `app_*_main()` 与 `kernel/app_registry.c::launcher_*` 函数重复定义,Phase D 后续版本会合并
2. `apps/*.c` 中部分文件名无 `_app` 后缀,与 Makefile `*_app.c` 通配规则不一致 — 已在 Phase B 标 DEPRECATED
3. `kernel/gui_apps.c` 用 `gui_app_*` 前缀(而不是 `gui_*`),因为它封装了窗口化应用,与 `gui/window.c` 的 `window_*` 区分