# os/ — 用户态上层组件

> 本目录在 Makefile 中由 `OS_C` 变量统一收集并**静态链接进内核**。
> 在 v0.8 模型中,所谓"用户态"实际上是**内核地址空间内的逻辑分层**,与 `kernel/` 在同一 ELF 镜像。
> 不依赖硬件;只依赖 `kernel/` 提供的系统调用 + `gui/` + `renderer/` + `sdk/`。

## 子目录布局

```
os/
├── fun_format.c/h        # .FUN 可执行容器格式的解析与加载器 (88 KB) — 替代 ELF 的精简格式
├── README.md             # 本文件
│
├── desktop/              # 桌面环境(图形会话)
│   ├── desktop.c/h       # 桌面会话生命周期 (init/run/exit),持有桌面状态结构
│   ├── gui_core.c/h      # 桌面级 GUI 工具:窗口定位/缩放/主题色/光标渲染等公共 API
│   ├── loading.c/h       # 启动加载画面 (splash → desktop 的过渡)
│   ├── start_menu.c/h    # 开始菜单:分类列出已注册应用,触发 launch
│   ├── taskbar.c/h       # 底部任务栏:已开窗口/托盘/时钟
│   └── window_mgr.c/h    # 桌面侧窗口管理:Z-order/focus/minimize(薄壳,真正逻辑在 gui/wm.c)
│
├── apps/                 # 桌面应用(图层相对独立)
│   ├── calculator.c/h    # 计算器(支持鼠标 + 键盘)
│   ├── file_manager.c/h  # 文件管理器
│   ├── paint.c/h         # 画板
│   ├── settings.c/h      # 系统设置(显示/鼠标/键盘/日期/关于)
│   ├── terminal.c/h      # 终端模拟器(包装 kernel/shell.c — 权威 shell)
│   └── text_editor.c/h   # 文本编辑器
│
└── services/             # 系统服务(独立线程式存在)
    ├── clipboard_service.c/h  # 剪贴板(支持文本 + 图像)
    ├── login_service.c/h      # 用户登录认证(包装 kernel/user.c)
    ├── notification.c/h       # 系统通知气泡
    └── power_service.c/h      # 电源管理(ACPI 包装)
```

## 关系与依赖图(精简)

```
                  ┌────────────────────────────┐
                  │   kernel/main.c            │
                  │   (services framework)    │
                  └────────────┬───────────────┘
                               │ 注册/启动
        ┌──────────────────────┼───────────────────────┐
        ▼                      ▼                       ▼
  desktop/desktop.c     services/login_service.c   apps/* (按需 launch)
        │
        ├── start_menu ── 调用 apps/* launch
        ├── taskbar    ── 跟随 gui/wm.c 的窗口事件
        └── window_mgr ── 包装 gui/window.c / wm.c

  apps/*    ──→   gui/* (widget/window/gfx/font/...)
              ──→   renderer/* (效果/合成/动画)
              ──→   sdk/* (高层 API 包装)
              ──→   os/fun_format.c (加载 .FUN 镜像)
```

## 与 apps/ 的区别

| 路径 | 设计初衷 | 当前角色 |
|------|---------|----------|
| `os/apps/*` | 桌面"窗口化"应用,带 GUI 与事件循环 | 主路径(被 `kernel/gui_apps.c` 注册到开始菜单) |
| `apps/*` (根) | 早期 CLI 命令与简单的"窗口应用"原型 | 部分还在用(`apps/init.c` 作为 PID 2 入口),其余被 `os/apps/` 取代 |

**约定**:
- 新增桌面应用应该放在 `os/apps/<name>.{c,h}`
- 新增纯命令行工具(无 GUI)应该放在 `apps/<name>_app.c`(Makefile `APPS_C` 通过 `*_app.c` 通配)
- 新增系统服务(独立线程 + 状态机)应该放在 `os/services/<name>_service.{c,h}`

## 已知约束 / 重构注意

1. **共享全局符号**:所有 `os/*` 静态链入 `kernel.elf`,因此**函数/全局变量名必须以模块前缀**(见各文件顶端注释)。
2. **GUI 入口**:桌面应用通过 `int xxx_app_run(void)` 入口被注册到 `kernel/app_registry.c`,而不是 main()。
3. **栈空间**:每个应用是内核线程,栈大小由 `kernel/process.c` 的 `thread_create()` 决定,大窗口应用可在 `app_init()` 中调用 `thread_extend_stack()`。
4. **持久化**:设置、剪贴板等需要落盘的服务,通过 `kernel/config.c` 与 `/etc/<service>.conf` 交互。

## Phase A/B/C/D/E 在本目录的落地

| Phase | 落地动作 | 涉及文件 |
|-------|----------|---------|
| A     | 本 README + 子目录内子 README | (本文件 + 子 README) |
| B     | 删除调试残留;统一头注释 | — |
| C     | `os/apps/settings.c` 拆分(按 SETTINGS_CAT_*) | `os/apps/settings.c/h` |
| C     | `os/fun_format.c` 不拆(单文件可读性尚可,只整理) | `os/fun_format.c/h` |
| D     | `os/desktop/*` 与 `gui/*` 边界标注 | `os/desktop/*.c` 顶端注释 |
| E     | README 同步到顶层 README.md | `README.md` |