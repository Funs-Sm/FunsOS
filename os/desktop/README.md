# os/desktop/ — 桌面环境(图形会话层)

> 在 `gui/wm.c` 与 `kernel/display_server.c` 之上的"用户看得见的那一层":
> 壁纸、图标、任务栏、开始菜单、加载动画、Z 序。
> 这一层的所有组件都是内核线程,由 `kernel/services.c` 的 service framework 调度。

## 文件职责

| 文件 | 职责 | 线程模型 |
|------|------|---------|
| `desktop.c/h` | 桌面会话主循环,持有 `desktop_state_t`(屏幕尺寸/工作区数/全屏应用) | 主线程 |
| `gui_core.c/h` | GUI 通用工具(颜色/光标/图标绘制 API);`os/apps/*` 也通过它访问基础绘制 | 库(无线程) |
| `loading.c/h` | 启动加载画面(splash → desktop 的过渡) | 短时线程 |
| `start_menu.c/h` | 开始菜单:扫描 `kernel/gui_apps.c` 的应用表,渲染分类菜单 | 弹窗线程 |
| `taskbar.c/h` | 底部任务栏:已开窗口缩略图 + 托盘 + 时钟 | 独立线程 |
| `window_mgr.c/h` | 桌面侧窗口操作的薄壳(包装 `gui/wm.c`) | 库 |

## 重构注意

- 这一层**不**应该直接调用 `kernel/vesa.c` 的 framebuffer;统一走 `gui/desktop_bridge.c`
- 所有图标路径必须是 `initrd:/path/to/icon.bmp` 前缀(以隔离真实文件系统路径)
- `start_menu.c` 不要硬编码应用列表 — 由 `kernel/app_registry.c` 提供