# os/apps/ — 桌面 GUI 应用主路径

> v0.8 起桌面应用的"权威位置";旧的同名文件在 `apps/` 目录已被本目录取代。
> 每个应用都是独立内核线程,有 `app_<name>_run()` 入口;通过 `kernel/app_registry.c` 注册到开始菜单。

## 应用清单

| 应用 | 源文件 | 功能 | 复杂度 |
|------|--------|------|--------|
| 计算器 | `calculator.c/h` | 基础算术(鼠标 + 键盘) | 中 |
| 文件管理器 | `file_manager.c/h` | 浏览/复制/删除/重命名 | 高(36 KB) |
| 画板 | `paint.c/h` | 鼠标绘制 + 调色板 + 撤销 | 中(32 KB) |
| 设置 | `settings.c/h` | 显示/鼠标/键盘/日期/关于 五大分类面板 | 高(58 KB,**计划拆分**) |
| 终端 | `terminal.c/h` | 包装 `kernel/shell.c` | 高(43 KB) |
| 文本编辑器 | `text_editor.c/h` | 多缓冲编辑 + 查找替换 | 高(41 KB) |

## 通用约定

1. **入口签名**:`int app_<name>_run(void)`(无参数,从 `kernel/app_registry.c` 调用)
2. **状态管理**:应用有独立 `static app_<name>_state_t`,在 `app_<name>_init()` 中初始化
3. **退出方式**:返回整数退出码,`< 0` 表示异常;由 `kernel/app_registry.c` 调度下一个应用
4. **资源释放**:每个应用必须在退出时关闭已打开的文件、释放分配的画布(`gui/window_t` 是引用计数,**不能**只关应用)

## Phase C 拆分目标

- `settings.c` 58 KB → `settings_display.c / settings_mouse.c / settings_keyboard.c / settings_datetime.c / settings_about.c`(沿用 `SETTINGS_CAT_*` 宏)
- `file_manager.c` 36 KB → 暂不拆(逻辑高度耦合)
- `paint.c` 32 KB → 暂不拆
- `terminal.c` 43 KB → 暂不拆
- `text_editor.c` 41 KB → 暂不拆