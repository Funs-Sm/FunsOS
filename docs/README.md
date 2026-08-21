# docs/ — 项目文档

> v0.8.1 起集中存放重构与架构说明的位置。**单文件级别**的说明放在该文件自身的头部注释中;
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
v0.8.1          ── 用户态+GUI+renderer 结构清理
                   ↑ 已完成

v0.8.2 (当前)  ── renderer/src/effect.c 实际拆分
                   ├─ Phase 1 (DONE): opacity 迁出 → effect_s8.c (2 KB, 自包含)
                   ├─ Phase 2: sections 9-12 (高级/色彩/阴影扩展/发光描边)
                   │           需要整组迁出(内部互相调用)
                   └─ Phase 3: sections 0-7 (与主模块互相依赖,高风险)
v0.8.3          ── kernel/shell.c 实际拆分(5~8 次迭代)
v0.8.4          ── renderer/src/widgets.c 按控件拆(15+ 文件)
v0.8.5          ── GUI 边界标准化(gui/window_server 独立化)

v0.9            ── 真正用户态进程隔离(init ELF / shell ELF 独立)
```