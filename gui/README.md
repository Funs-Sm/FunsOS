# gui/ — 基础图形子系统

> 提供"无窗口管理"的图形原语,只与 framebuffer(`drivers/vesa.c`)打交道。
> 窗口/合成/光标/主题等更上层逻辑在 `os/desktop/*` 与 `kernel/gui_apps.c`。
> `gui/*` 不依赖 `os/*` 与 `kernel/gui_apps.c`。

## 模块依赖图

```
drivers/vesa.c (framebuffer)
        ▲
        │
   gui/gfx.c ── gui/gfx3d.c ── gui/png.c ── gui/jpeg.c
        ▲                          ▲
        │                          │
   gui/window.c ── gui/compositor.c
        ▲
        │
   gui/wm.c (窗口管理器)
        ▲
        │
   gui/widget.c (控件系统:按钮/标签/列表/输入框)
        ▲
        │
   gui/theme.c / gui/cursor.c / gui/bitmap.c / gui/font.c
```

## 文件职责

| 文件 | 职责 |
|------|------|
| `gfx.c/h` | 2D 原语:像素/线/矩形/圆/椭圆/多边形/Bezier/位图 blit。**所有渲染的最低层**。 |
| `gfx3d.c/h` | 3D 软件渲染:透视投影/Z-buffer/Phong/纹理映射 |
| `png.c/h` | PNG 解码(软件,自实现,无 libpng 依赖) |
| `jpeg.c/h` | JPEG 解码(软件,自实现,无 libjpeg 依赖) |
| `font.c/h` | 内置等宽字体位图;不依赖 freetype |
| `freetype_mini.c/h` | TrueType 字体子集渲染(精简版 FreeType 2) |
| `bitmap.c/h` | 位图结构与操作(blit/alpha blend/色彩转换) |
| `window.c/h` | 窗口数据结构 + 单窗口绘制(不含管理器) |
| `wm.c/h` | 窗口管理器(Z-order/focus/事件分发/拖动) |
| `compositor.c/h` | 合成器(多窗口 + 阴影 + 圆角 + alpha blend) |
| `widget.c/h` | 控件(button/label/list/scrollbar/textfield/checkbox) |
| `theme.c/h` | UI 主题(颜色/字号/控件外观) |
| `cursor.c/h` | 鼠标光标(箭头/手型/等待/文本光标/动画) |
| `window_server.c/h` | **薄壳**:把窗口/合成器封装成"显示服务器"接口,供 `kernel/display_server.c` 调用 |
| `desktop_bridge.c/h` | **薄壳**:把 `gui/wm.c` 的事件桥接到 `os/desktop/desktop.c` |
| `renderer_bridge.c/h` | **薄壳**:把 `gui/gfx.c` 桥接到 `renderer/src/*` 高级效果 |

## 与 `renderer/` 的分工

| gui/ | renderer/ |
|------|-----------|
| 即时模式画线/画点/画矩形 | 保留模式场景图 |
| 单个窗口的事件循环 | 多场景的合成 + 动画 + 着色器 + 粒子 + 路径 |
| 同步绘制 | 离屏帧缓存 + dirty region |
| 主题色固定 | CSS-风格主题 + 渐变 + 阴影 |

**两者并存**:`gui/` 负责窗口与控件;`renderer/` 负责场景级效果(应用启动画面、3D 桌面背景、特效按钮)。
切换策略在 `kernel/gui_apps.c` 的 `app_flags`:带 `APP_FLAG_REUSE_RENDERER` 的应用走 renderer 路径,否则走 gui/ 直绘。

## Phase C 拆分计划

| 大文件 | 大小 | 拆分方案 |
|--------|------|---------|
| `gui/font.c` 14 KB | 暂不拆,内聚度高 |
| `gui/widget.c` 15 KB | Phase C1:按控件类拆 |
| `gui/wm.c` 20 KB | Phase C2:按职责拆 `wm_focus.c / wm_zorder.c / wm_event.c / wm_drag.c` |
| `gui/png.c` 17 KB | 不拆(纯算法) |
| `gui/jpeg.c` 22 KB | 不拆(纯算法) |
| `gui/gfx3d.c` 20 KB | 不拆(纯算法) |

## 头文件分组约定

- 内部使用 `#include "gui/widget.h"` 这种带子目录前缀
- 外部模块应只 include `gui/desktop_bridge.h` 或 `gui/renderer_bridge.h`,**不要**直接 include `gui/wm.h` 或 `gui/compositor.h`(它们在重构期间接口可能变)
- 所有 GUI 模块的初始化入口是 `int xxx_init(int width, int height, uint32_t *fb, uint32_t pitch)`,由 `kernel/display_server.c` 统一调用