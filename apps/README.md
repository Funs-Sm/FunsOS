# apps/ — 早期应用与 CLI 工具实现

> Makefile 通过 `APPS_C = $(wildcard apps/*_app.c) apps/init.c` 自动收集。
> 命名约定:每个应用同时提供 `apps/<name>.c`(CLI 命令行实现,被 `kernel/shell.c` 通过 exec 调用)
> 和 `apps/<name>_app.c`(GUI 应用入口,被 `kernel/gui_apps.c` 注册到桌面)。
> **该目录正逐步被 `os/apps/` 取代**;新代码请优先放到 `os/apps/`。

## 文件清单

| 文件 | 类别 | 角色 | 状态 |
|------|------|------|------|
| `init.c` | 系统入口 | `app_init_main(argc,argv)` — PID 2 的 init 进程 | 保留 |
| `desktop.c` | GUI 应用 | 旧版桌面入口(已被 `os/desktop/desktop.c` 取代) | **可移除** |
| `terminal.c` | GUI 应用 | 旧版终端入口(已被 `os/apps/terminal.c` 取代) | **可移除** |
| `filemgr.c` | GUI 应用 | 旧版文件管理器入口(已被 `os/apps/file_manager.c` 取代) | **可移除** |
| `notepad.c` | GUI 应用 | 旧版记事本(已被 `os/apps/text_editor.c` 取代) | **可移除** |
| `paint.c` | GUI 应用 | 旧版画板(已被 `os/apps/paint.c` 取代) | **可移除** |
| `calc.c` | GUI 应用 | 旧版计算器(已被 `os/apps/calculator.c` 取代) | **可移除** |
| `snake.c` | 游戏 | 贪吃蛇(无 GUI 替代,保留) | 保留 |
| `shell.c` | CLI 应用 | 旧版 shell 入口(已被 `kernel/shell.c` 取代) | **可移除** |
| `cat.c` / `cat_app.c` | CLI 命令 | cat | 保留 |
| `cp.c` / `cp_app.c` | CLI 命令 | cp | 保留 |
| `echo.c` / `echo_app.c` | CLI 命令 | echo | 保留 |
| `ls.c` / `ls_app.c` | CLI 命令 | ls | 保留 |
| `mkdir.c` / `mkdir_app.c` | CLI 命令 | mkdir | 保留 |
| `mv.c` / `mv_app.c` | CLI 命令 | mv | 保留 |
| `rm.c` / `rm_app.c` | CLI 命令 | rm | 保留 |
| `touch.c` | CLI 命令 | touch | 保留 |
| `help.c` | CLI 命令 | 内置帮助 | 保留 |
| `date_app.c` | CLI 命令 | date | 保留 |
| `grep_app.c` | CLI 命令 | grep | 保留 |
| `head_app.c` | CLI 命令 | head | 保留 |
| `wc_app.c` | CLI 命令 | wc | 保留 |
| `gfx_adapter.c/h` | 共享库 | GUI 应用的图形适配层 | 保留 |
| `gui_common.h` | 共享头 | GUI 公共宏/类型 | 保留 |
| `user_syscall.h` | 共享头 | 用户态系统调用号定义(与 `sdk/include/funsos.h` 同步) | 保留 |
| `crt0.asm` | 启动 | 用户态 C runtime 启动(`_start`),从内核 iret 后的栈读取 argc/argv 后调用 `main()`,`main` 返回后调用 `SYS_EXIT` 退出 | 已可用(配合 `process_exec`) |
| `Makefile` | 构建 | 该目录**未被顶层 Makefile 调用**;`apps:` 目标会进这里但当前未在主流程里触发 | 保留(历史) |

## 计划重构动作(Phase B/C)

- **Phase B**:把"已被取代"标记为可移除的 GUI 文件(`desktop.c/terminal.c/filemgr.c/notepad.c/paint.c/calc.c/shell.c`)在文件顶端加 `/* DEPRECATED:` 注释,**不立即删除**(因为 `kernel/gui_apps.c` 可能仍引用它们作为 fallback)
- **Phase C**:`kernel/gui_apps.c` 改造为数据驱动(扫描 `os/apps/`),这一步完成后 `apps/` 中被标 DEPRECATED 的文件就可删
- **Phase D**:统一 CLI 错误返回值(`-errno` 约定)
- **Phase E**:本 README 同步到顶层 README.md

## 临时约束

- 每个 `_app.c` 必须实现 `int app_<name>_run(void);` 函数,这是 `kernel/gui_apps.c` 寻找的入口
- 每个 `*.c`(无 `_app` 后缀)是 CLI 命令实现,函数签名约定为 `int cmd_<name>(int argc, char **argv)`,由 `kernel/shell.c` 调用