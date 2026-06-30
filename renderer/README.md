# FunRender - 独立 UI 渲染引擎

<p align="center">
  <img src="https://img.shields.io/badge/FunRender-v1.3-blue" alt="Version"/>
  <img src="https://img.shields.io/badge/Status-Stable-green" alt="Status"/>
  <img src="https://img.shields.io/badge/Widgets-30%2B-purple" alt="Widget Count"/>
  <img src="https://img.shields.io/badge/Themes-9-yellow" alt="Theme Count"/>
  <img src="https://img.shields.io/badge/Modules-22-orange" alt="Module Count"/>
  <img src="https://img.shields.io/badge/Kernel_Independent-Yes-success" alt="Kernel Independent"/>
</p>

<p align="center">
  <strong>模块化、内核独立的跨平台 UI 控件与渲染引擎</strong><br/>
  <em>A modular, kernel-independent, cross-platform UI widget and rendering engine</em>
</p>

---

**Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.**
Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.

---

## v1.3 更新摘要

**2025-07 更新 | 新增控件、主题、动画、文本、布局全面增强**

### 新功能一览

| 模块 | 新增功能 |
|------|----------|
| **控件系统** | 新增复选框、单选框、增强按钮（5 种样式）、增强标签（自动换行/对齐）、增强输入框（密码/多行/光标） |
| **主题系统** | 运行时主题过渡动画、主题类型检测（深/浅/高对比）、主题监听器、主题直接混合 API、高对比度主题 |
| **动画系统** | 29 种缓动函数（四次/五次/正弦/圆形/指数/回弹）、关键帧动画、动画序列 |
| **图形效果** | 内阴影、运动模糊（方向+强度）、描边效果（外/内/居中）、颜色调整（亮度/对比度/饱和度/色相/伽马）、径向模糊 |
| **文本渲染** | `fr_text.h` 新头文件、对齐方式（左/中/右/两端）、自动换行（单词/字符）、文本省略（首/中/尾）、富文本（粗体/斜体/下划线/颜色/大小）、文本测量 API |
| **布局管理器** | Flex 布局增强、子项 flex_grow/flex_shrink、子项 margin、padding 内边距、水平/垂直独立间距、尺寸约束 |

---

## 简介

**FunRender** 是 FunsOS 操作系统中负责 **UI 控件渲染与管理的独立引擎**。它与 FunsOS 内核完全解耦——不编译进内核映像，而是作为独立的用户态/上层库存在，通过 `gui/gfx.h` 和 `gui/gfx3d.h` 提供的底层图形 API 进行像素级绘制。

这种**内核独立性**设计带来的优势：

- **安全性**：UI 渲染漏洞不会危及内核稳定性
- **可维护性**：UI 引擎可独立升级，无需重新编译内核
- **复用性**：理论上可将 FunRender 移植到其他图形后端或操作系统
- **可测试性**：可在宿主机上进行单元测试和可视化预览

FunRender 提供了 **30+ 种 UI 控件类型**、**6 套内置主题**、**5 种动画效果**、**灵活的布局系统**以及**完整的窗口管理**能力，足以支撑从简单对话框到复杂桌面应用的各种 UI 需求。

---

## 设计理念

### 渲染引擎与内核分离的理由

| 传统内核集成方式 | FunRender 独立方式 |
|-----------------|-------------------|
| UI 代码编译进内核，增大内核体积 | 独立库，按需加载 |
| UI Bug 可能导致内核崩溃 | UI 异常仅影响应用进程 |
| 修改 UI 需要重新编译整个内核 | 独立修改、独立部署 |
| 无法在开发机上预览 UI | 可在宿主机进行 UI 开发与调试 |
| 内核态图形调用，上下文切换开销大 | 用户态渲染，更高效的开发体验 |

### 核心设计原则

1. **模块化 (Modularity)** — 每个功能域是独立的模块（widget/layout/theme/animation...），可单独替换
2. **数据驱动 (Data-Driven)** — 控件属性、主题配色、动画曲线均由数据结构定义，便于序列化和热更新
3. **回调驱动 (Callback-Driven)** — 事件处理采用函数指针回调模式，解耦事件源与业务逻辑
4. **组合优于继承 (Composition over Inheritance)** — 控件通过父子关系树组织，而非深层继承体系
5. **即时模式与保留模式结合** — 布局计算使用保留模式（控件树持久存在），渲染可采用脏区域优化的即时模式

---

## 渲染器架构

FunRender 采用分层架构，每一层职责单一且接口明确：

```
┌─────────────────────────────────────────────────────────────┐
│                    应用程序层 (Application)                    │
│              使用 FunRender API 构建用户界面                    │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               窗口管理层 (Window Management)                   │
│         fr_window.c — 窗口创建/销毁/标题栏/边框/阴影/Z-order    │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               合成器层 (Compositor)                           │
│   fr_compositor.c — Z-order排序/Alpha混合/脏区域跟踪/HW加速     │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               控件层 (Widget Layer)                           │
│   fr_widgets.c — 30+ 种控件的创建/属性/渲染/事件处理            │
│   ┌────────┬────────┬────────┬────────┬────────┬───┐        │
│   │ Button │ Label  │Textbox │Checkbox│ Slider │...│ 30+   │
│   └────────┴────────┴────────┴────────┴────────┴───┘        │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               布局层 (Layout Layer)                           │
│   fr_layout.c — HBox/VBox/Grid/Anchor/Flex 弹性布局           │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               主题层 (Theme Layer)                            │
│   fr_theme.c + themes/ — 颜色/字体/圆角/阴影/间距 定义          │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               动画层 (Animation Layer)                        │
│   fr_animation.c — 淡入淡出/滑动/缩放/旋转/弹跳 + 缓动函数      │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               事件层 (Event Layer)                            │
│   fr_events.c — 键盘/鼠标/触摸/手势/焦点的分发与冒泡            │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               输入层 (Input Layer)                            │
│   fr_input.c — 原始输入设备数据的采集与标准化                  │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               文本/画布层 (Text & Canvas Layer)               │
│   text.c + canvas.c — 文本测量/渲染/换行 + 2D 画布绘图原语     │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               上下文层 (Context Layer)                        │
│   fr_context.c — 渲染上下文初始化/帧缓冲管理/状态栈            │
└─────────────────────────────────────────────────────────────┘
                       │
                       ▼
        ┌──────────────────────────┐
        │   底层图形后端 (Backend)   │
        │  gfx.h / gfx3d.h (FunsOS) │
        │  或其他图形 API (可移植)   │
        └──────────────────────────┘
```

---

## 组件详解

### 1. 上下文层 (Context) — `fr_context.c` / `fr_context.h`

渲染上下文是 FunRender 的根基，管理整个渲染生命周期的全局状态。

| 功能 | 描述 |
|------|------|
| `fr_init(width, height, fb)` | 初始化渲染引擎，绑定帧缓冲区 |
| `fr_shutdown(ctx)` | 销毁渲染上下文，释放所有资源 |
| `fr_begin_frame(ctx)` | 开始新一帧渲染 (清除脏标记) |
| `fr_end_frame(ctx)` | 结束当前帧 (提交到帧缓冲) |
| 状态栈 | 保存/恢复裁剪区域、变换矩阵、颜色状态 |

### 2. 控件层 (Widgets) — `fr_widgets.c` / `fr_widgets.h`

FunRender 的核心——**30 种 UI 控件类型**，每种控件都有统一的基类 (`fr_widget_t`) 和扩展属性。详见「控件列表」章节。

### 3. 布局层 (Layout) — `fr_layout.c` / `fr_layout.h`

自动计算子控件的位置和大小。

| 布局类型 | 函数 | 描述 |
|----------|------|------|
| **水平布局 (HBox)** | `fr_layout_hbox(parent, spacing, margin)` | 子控件水平排列，支持均匀分布 |
| **垂直布局 (VBox)** | `fr_layout_vbox(parent, spacing, margin)` | 子控件垂直排列 |
| **网格布局 (Grid)** | `fr_layout_grid(parent, cols, rows, spacing)` | 二维网格排列 |
| **锚点布局 (Anchor)** | `fr_layout_anchor(parent, anchor_flags)` | 相对于父容器边缘锚定 (上/下/左/右/居中) |

### 4. 主题层 (Theme) — `fr_theme.c` / `fr_theme.h` + `themes/`

全局视觉风格管理系统。详见「主题系统」章节。

### 5. 动画层 (Animation) — `fr_animation.c` / `fr_animation.h`

为控件属性变化提供平滑过渡效果。详见「动画系统」章节。

### 6. 事件层 (Events) — `fr_events.c` / `fr_events.h`

统一的事件分发系统，支持事件冒泡和捕获。

| 事件类型 | 描述 |
|----------|------|
| `FR_EVENT_MOUSE_MOVE` | 鼠标移动 |
| `FR_EVENT_MOUSE_DOWN` | 鼠标按下 (左/中/右键) |
| `FR_EVENT_MOUSE_UP` | 鼠标释放 |
| `FR_EVENT_MOUSE_WHEEL` | 鼠标滚轮滚动 |
| `FR_EVENT_KEY_DOWN` | 键盘按下 |
| `FR_EVENT_KEY_UP` | 键盘释放 |
| `FR_EVENT_KEY_PRESS` | 字符输入 (已翻译的按键) |
| `FR_EVENT_FOCUS_GAIN` | 控件获得焦点 |
| `FR_EVENT_FOCUS_LOST` | 控件失去焦点 |
| `FR_EVENT_TIMER` | 定时器触发 |
| `FR_EVENT_CUSTOM` | 用户自定义事件 |

### 7. 合成器 (Compositor) — `fr_compositor.c` / `fr_compositor.h`

将多个窗口/图层合成为最终帧缓冲图像。

| 能力 | 描述 |
|------|------|
| **Z-Order 排序** | 按深度排序窗口，正确处理遮挡关系 |
| **Alpha 混合** | 支持正常/叠加/正片叠底等多种混合模式 |
| **脏区域跟踪 (Damage Tracking)** | 仅重绘发生变化的区域，大幅提升性能 |
| **硬件加速提示** | 标记可通过 GPU 加速的操作 |
| **窗口装饰** | 自动绘制标题栏、边框、阴影、圆角 |
| **光标合成** | 将鼠标光标合成到最终画面顶层 |

### 8. 输入层 (Input) — `fr_input.c` / `fr_input.h`

原始输入设备的抽象层，将不同来源的输入统一为标准事件格式。

### 9. 文本层 (Text) — `text.c`

Unicode 文本的测量、换行、光标定位与渲染。

### 10. 画布层 (Canvas) — `canvas.c`

2D 绘图原语的直接接口，供高级控件内部使用。

### 11. 窗口管理层 (Window) — `fr_window.c`

顶级窗口的完整生命周期管理，包括窗口装饰和非客户区事件处理。

---

## 控件列表

FunRender 提供了 **30 种 UI 控件**，覆盖桌面应用的所有常见需求：

| # | 控件类型 | 类型常量 | 描述 | 主要属性 |
|:-:|----------|----------|------|----------|
| 1 | **Button 按钮** | `FR_WIDGET_BUTTON` | 可点击的命令触发控件 | text, border_radius, hover_color, press_color |
| 2 | **Label 标签** | `FR_WIDGET_LABEL` | 纯文本显示控件 | text, wrap (自动换行), alignment (对齐方式) |
| 3 | **Textbox 文本框** | `FR_WIDGET_TEXTBOX` | 单行/多行文本输入 | cursor_pos, selection, max_length, password_mode, multiline |
| 4 | **Checkbox 复选框** | `FR_WIDGET_CHECKBOX` | 勾选/取消勾选 | checked 状态 |
| 5 | **Radio 单选按钮** | `FR_WIDGET_RADIO` | 互斥选择的单选组 | selected, group_id |
| 6 | **Slider 滑块** | `FR_WIDGET_SLIDER` | 数值范围滑动选择 | min_val, max_val, value, orientation, tick_interval |
| 7 | **HSlider 水平滑块** | `FR_WIDGET_HSLIDER` | 专用水平方向滑块 | 同 Slider + inverted 反向 |
| 8 | **VSlider 垂直滑块** | `FR_WIDGET_VSLIDER` | 专用垂直方向滑块 | 同 Slider + inverted 反向 |
| 9 | **Progress 进度条** | `FR_WIDGET_PROGRESS` | 任务进度百分比指示 | value, max_val, show_percent, fill_color |
| 10 | **Progress2 增强进度条** | `FR_WIDGET_PROGRESS2` | 带动画过渡的增强版进度条 | animated, display_value (动画插值) |
| 11 | **Combobox 下拉框** | `FR_WIDGET_COMBOBOX` | 下拉选择列表 | items[], selected_index, dropdown_open |
| 12 | **Listbox 列表框** | `FR_WIDGET_LISTBOX` | 可滚动列表选择 | items[], selected_index, top_item, visible_items |
| 13 | **Table 表格** | `FR_WIDGET_TABLE` | 行列式数据表格 | cells[][], col_widths[], selected_row, header_height |
| 14 | **TabView 标签页** | `FR_WIDGET_TABVIEW` | 多页面标签切换 | tab_labels[], active_tab, tab_pages[] |
| 15 | **TabControl 标签容器** | `FR_WIDGET_TAB_CONTROL` | 增强版标签页 (可关闭/多位置) | closable, tab_position (顶/底/左/右) |
| 16 | **Menu 菜单** | `FR_WIDGET_MENU` | 下拉/弹出菜单 | item_labels[], item_types[], visible |
| 17 | **Toolbar 工具栏** | `FR_WIDGET_TOOLBAR` | 按钮工具条 | buttons[], orientation |
| 18 | **Toolbar2 增强工具栏** | `FR_WIDGET_TOOLBAR2` | 带图标/分隔线的增强工具栏 | icon_size, show_text, separator_indices[] |
| 19 | **Statusbar 状态栏** | `FR_WIDGET_STATUSBAR` | 底部状态信息栏 | sections[][64], section_widths[] |
| 20 | **Statusbar2 增强状态栏** | `FR_WIDGET_STATUSBAR2` | 带彩色分段和拖动手柄 | section_colors[], show_grip |
| 21 | **Dialog 对话框** | `FR_WIDGET_DIALOG` | 模态/非模态对话框 | dialog_type (普通/模态), result, content |
| 22 | **Scrollbar 滚动条** | `FR_WIDGET_SCROLLBAR` | 滚动位置指示器 | orientation, min/max/value, thumb_size |
| 23 | **Treeview 树形视图** | `FR_WIDGET_TREEVIEW` | 层级数据展开/折叠 | root (tree_node), selected_node, visible_depth |
| 24 | **TreeView 层级树视图** | `FR_WIDGET_TREE_VIEW` | 增强版树视图 | show_lines, show_root_handles, indent_px |
| 25 | **Container 容器** | `FR_WIDGET_CONTAINER` | 通用子控件容器 | 无特殊属性，纯容器语义 |
| 26 | **Spinbox 数值微调框** | `FR_WIDGET_SPINBOX` | 带加减按钮的数字输入 | value, min/max, step, wrap (循环) |
| 27 | **Splitter 分割器** | `FR_WIDGET_SPLITTER` | 可拖动的面板分割线 | orientation, position, min_pane_size, pane_first/second |
| 28 | **MDI Area 多文档区域** | `FR_WIDGET_MDI_AREA` | 多子窗口管理区域 | children[], arrangement (级联/平铺/水平/垂直) |
| 29 | **Calendar 日历** | `FR_WIDGET_CALENDAR` | 日期选择器控件 | year/month/day, week_numbers, header_visible |
| 30 | **Color Picker 颜色选择器** | `FR_WIDGET_COLOR_PICKER` | RGBA 颜色选择 (色轮/滑块/调色板) | r/g/b/a, mode (HSV/RGB/Palette), recent_colors[] |

### 控件基类结构

所有控件均继承自 `fr_widget_t` 基类：

```c
typedef struct fr_widget {
    uint32_t type;              // 控件类型 (FR_WIDGET_BUTTON 等)
    uint32_t state;             // 状态标志 (VISIBLE/ENABLED/FOCUSED/HOVERED/PRESSED/CHECKED/DIRTY)
    fr_rect_t bounds;           // 位置和尺寸 {x, y, w, h}
    char text[256];             // 文本内容
    fr_color_t fg_color;        // 前景色
    fr_color_t bg_color;        // 背景色
    struct fr_widget *parent;   // 父控件指针
    struct fr_widget *first_child;  // 第一个子控件
    struct fr_widget *next_sibling; // 下一个兄弟控件

    // 事件回调函数指针
    void (*on_click)(struct fr_widget *widget, void *data);
    void (*on_change)(struct fr_widget *widget, void *data);
    void (*on_key)(struct fr_widget *widget, void *data);
    void (*on_focus)(struct fr_widget *widget, int gained);
    void *user_data;            // 用户自定义数据

    // 布局约束
    void *layout;               // 布局策略指针
    int min_width, min_height;  // 最小尺寸
    int max_width, max_height;  // 最大尺寸

    // 渲染
    void (*render)(struct fr_widget *widget, fr_context_t *ctx);
    void (*handle_event)(struct fr_widget *widget, void *event);
} fr_widget_t;
```

---

## 主题系统

FunRender 内置 **9 套主题**，每套主题定义了一整套视觉变量：

### 主题列表

| 主题名称 | 文件 | 描述 | 适用场景 |
|----------|------|------|----------|
| **Default (默认)** | `themes/default.h` | 蓝色调经典主题，类似 Windows 7 Aero 风格 | 日常使用、默认首选 |
| **Dark (暗色)** | `themes/dark.h` | 深灰/近黑背景，护眼暗色主题 | 夜间使用、开发者偏好 |
| **Light (亮色)** | `themes/light.h` | 白色明亮主题，极简扁平风格 | 日间使用、简约美学 |
| **Ocean (海洋)** | `themes/ocean.h` | 深海蓝背景，青色强调色调 | 清爽专业风格 |
| **Forest (森林)** | `themes/forest.h` | 自然绿色调，大地色背景 | 环保/健康类应用 |
| **Sunset (日落)** | `themes/sunset.h` | 暖色橙色强调，暖灰底色，大圆角 | 温馨舒适风格 |
| **Monochrome (高对比度)** | `themes/monochrome.h` | 纯黑白高对比度，大字粗边框 | 无障碍辅助功能 |
| **Cyberpunk (赛博朋克)** | `themes/cyberpunk.h` | 深黑背景，霓虹粉/青强调，等宽字体 | 开发者/科技风格 |
| **Retro (复古蒸汽波)** | `themes/retro.h` | 粉彩柔和色调，大圆角，紫色强调 | 怀旧/创意风格 |

### 主题高级功能 (v1.2 新增)

| 功能 | 描述 |
|------|------|
| **主题预览** | `fr_theme_preview()` 提取 8 种关键颜色用于 UI 预览 |
| **主题导出/导入** | 二进制格式 (FRTH) 导出导入，支持主题备份与分享 |
| **主题混合** | `fr_theme_blend()` 按比例混合两个主题的颜色、字体与度量值 |
| **自动检测** | `fr_theme_auto_detect()` 基于时间自动选择深色/浅色主题 |

### 主题变量

每套主题定义以下视觉变量：

```c
typedef struct fr_theme {
    /* ---- 颜色 ---- */
    fr_color_t window_bg;           // 窗口背景色
    fr_color_t window_border;       // 窗口边框色
    fr_color_t titlebar_bg;         // 标题栏背景
    fr_color_t titlebar_text;       // 标题栏文字
    fr_color_t button_normal;       // 按钮常态
    fr_color_t button_hover;        // 按钮悬停
    fr_color_t button_pressed;      // 按钮按下
    fr_color_t button_text;         // 按钮文字
    fr_color_t input_bg;            // 输入框背景
    fr_color_t input_border;        // 输入框边框
    fr_color_t input_focus;         // 输入框聚焦高亮
    fr_color_t text_primary;        // 主要文字颜色
    fr_color_t text_secondary;      // 次要文字颜色
    fr_color_t accent;              // 强调色 (超链接、选中项)
    fr_color_t danger;              // 危险/警告色 (删除、错误)
    fr_color_t success;             // 成功色
    fr_color_t scrollbar_track;     // 滚动条轨道
    fr_color_t scrollbar_thumb;     // 滚动条滑块

    /* ---- 字体 ---- */
    char font_name[32];             // 字体族名称
    int font_size;                  // 默认字号
    int font_size_small;            // 小字号
    int font_size_large;            // 大字号

    /* ---- 间距 ---- */
    int padding;                    // 内边距
    int margin;                     // 外边距
    int spacing;                    // 控件间距
    int border_radius;              // 圆角半径
    int shadow_offset;              // 阴影偏移
    int shadow_blur;                // 阴影模糊半径
    int titlebar_height;            // 标题栏高度
    int border_width;               // 边框宽度

    /* ---- 窗口装饰 ---- */
    int window_shadow;              // 窗口阴影开关
    int corner_radius;              // 窗口圆角
} fr_theme_t;
```

### 主题切换与覆盖

```c
// 全局主题切换
fr_set_theme(ctx, "dark");         // 切换到 Dark 主题
fr_set_theme(ctx, "light");        // 切换到 Light 主题
fr_set_theme(ctx, "default");      // 切回默认主题

// 单个控件覆盖主题
fr_set_color(my_button, FR_RGB(255, 0, 0), FR_RGB(240, 240, 240));  // 红色按钮

// 全局字体设置
fr_set_font(ctx, "Sans", 14);      // 设置默认字体
```

---

## 动画系统

FunRender 内置动画框架，支持多种动画类型和缓动曲线。

### 支持的动画类型

| 动画类型 | 描述 | 典型用途 |
|----------|------|----------|
| **Fade (淡入淡出)** | 透明度从 0->1 或 1->0 过渡 | 窗口出现/消失、提示框 |
| **Slide (滑动)** | 控件从一个位置滑入另一个位置 | 侧边栏展开/收起、菜单弹出 |
| **Scale (缩放)** | 控件从小到大或从大到小变化 | 对话框弹出、按钮点击反馈 |
| **Rotate (旋转)** | 控件绕中心轴旋转 | 加载动画、特效 |
| **Bounce (弹性)** | 控件带弹性阻尼运动到达目标位置 | 通知弹出、趣味交互 |

### 缓动函数

| 缓动函数 | 曲线形状 | 描述 |
|----------|:--------:|------|
| `FR_EASE_LINEAR` | -------- | 匀速线性 |
| `FR_EASE_IN_QUAD` | 加速 | 二次方缓入 (先慢后快) |
| `FR_EASE_OUT_QUAD` | 减速 | 二次方缓出 (先快后慢) |
| `FR_EASE_INOUT_QUAD` | 加速再减速 | 二次方缓入缓出 |
| `FR_EASE_IN_CUBIC` | 加速 (更陡) | 三次方缓入 |
| `FR_EASE_OUT_CUBIC` | 减速 (更陡) | 三次方缓出 |
| `FR_EASE_INOUT_CUBIC` | 加速再减速 (更陡) | 三次方缓入缓出 |
| `FR_EASE_OUT_BOUNCE` | 弹性回弹 | 弹性缓出 (过冲后回落) |
| `FR_EASE_OUT_ELASTIC` | 振荡衰减 | 弹性缓出 (振荡衰减) |

### 动画 API

```c
// 创建动画
fr_anim_t *anim = fr_animate(
    target_widget,           // 目标控件
    "opacity",               // 动画属性名
    0.0, 1.0,               // 起始值 -> 目标值
    500,                     // 持续时间 (毫秒)
    FR_EASE_OUT_CUBIC        // 缓动函数
);

// 动画完成回调
fr_on_animation_complete(anim, on_fade_done, user_data);

// 手动控制
fr_anim_pause(anim);
fr_anim_resume(anim);
fr_anim_cancel(anim);
```

---

## 输入系统

FunRender 的输入系统统一处理来自多种输入设备的数据：

### 支持的输入设备

| 设备类型 | 支持情况 | 详情 |
|----------|:--------:|------|
| **键盘 (Keyboard)** | 完整 | 按下/释放/重复按键；支持修饰键 (Shift/Ctrl/Alt)；快捷键组合 |
| **鼠标 (Mouse)** | 完整 | 移动/按下/释放/双击/三击；左/中/右键区分；滚轮 (垂直+水平) |
| **触摸 (Touch)** | 计划中 | 单指/多点触控；手势识别 (捏合/滑动/旋转) |
| **手写笔 (Stylus)** | 计划中 | 压感、倾斜角度、橡皮擦模式 |
| **游戏手柄 (Gamepad)** | 计划中 | 按键/摇杆/扳机/震动反馈 |

### 焦点管理

```
焦点流转规则:
  Tab 键        -> 按控件 Z-order 顺序向前切换焦点
  Shift+Tab     -> 向后切换焦点
  鼠标点击      -> 点击的控件立即获得焦点
  窗口激活      -> 激活窗口时聚焦该窗口的上一个焦点控件
  焦点可见性    -> 当焦点控件不可见时自动转移焦点到最近可见控件
  Modal Dialog  -> 模态对话框激活时，焦点锁定在其范围内
```

### 事件分发模型

FunRender 采用**冒泡式事件分发**：

```
原始输入 (鼠标点击坐标: 200, 150)
    |
    v
Input 层: 转换为 FR_EVENT_MOUSE_DOWN 事件
    |
    v
Compositor 层: 确定命中目标 (hit-test) -> 找到 Widget C
    |
    v
Event 层: 开始冒泡
    |
    +-- Widget C.on_click()        <- 先处理 (目标控件)
    +-- Container B.on_click()      <- 冒泡到父容器
    +-- Window A.on_click()         <- 冒泡到窗口根

(任一 handler 返回 EVENT_HANDLED 可阻止继续冒泡)
```

---

## 合成器

合成器是 FunRender 的渲染管线末端，负责将所有可视元素合并为最终帧。

### 核心能力

#### Z-Order 排序

窗口按照从底到顶的 Z-order 排列，保证正确的遮挡关系：
- **Normal 窗口**: 按创建顺序或用户手动调整
- **TopMost 窗口**: 始终位于普通窗口之上
- **Modal 对话框**: 位于父窗口之上，半透明遮罩下层
- **系统提示 (Tooltip/Toast)**: 最高优先级

#### Alpha 混合模式

| 模式 | 名称 | 公式 | 用途 |
|------|------|------|------|
| `FR_BLEND_NORMAL` | 正常混合 | `src x a + dst x (1-a)` | 默认窗口合成 |
| `FR_BLEND_ADD` | 叠加 | `src + dst` | 发光效果、高亮 |
| `FR_BLEND_MULTIPLY` | 正片叠底 | `src x dst` | 阴影、暗化 |
| `FR_BLEND_SCREEN` | 滤色 | `1 - (1-src) x (1-dst)` | 光效、提亮 |
| `FR_COMPOSITE_OVERLAY` | 叠加 | `dst<0.5? 2*src*dst : 1-2*(1-src)*(1-dst)` | 对比度增强 |
| `FR_COMPOSITE_DARKEN` | 变暗 | `min(src, dst)` | 暗化效果 |
| `FR_COMPOSITE_LIGHTEN` | 变亮 | `max(src, dst)` | 提亮效果 |

#### 脏区域跟踪

合成器维护一个**脏区域矩形集合**（16 个脏矩形槽位，支持区域合并算法），仅重绘发生变化的部分。支持双缓冲机制（前/后缓冲区交换），并跟踪统计信息（blit/blend/flush 计数）：

```c
// 标记某个控件需要重绘
fr_invalidate(button_widget);          // 单个控件变脏
fr_invalidate_all(ctx);                // 整屏重绘

// 合成器自动:
// 1. 收集所有 dirty 控件的边界矩形 (最多 16 个槽位)
// 2. 合并为最小脏区域集合 (区域合并算法)
// 3. 双缓冲: 在后缓冲区执行像素合成，完成后交换到前缓冲区
// 4. 仅对脏区域执行像素合成
// 5. 性能提升: 通常只需重绘屏幕 5-20% 的面积
// 6. 统计: 自动记录 blit/blend/flush 操作次数
```

#### 硬件加速提示

```c
// 标记可加速的操作
fr_compositor_set_hint(ctx, FR_HW_BLIT);       // BitBLIT 加速
fr_compositor_set_hint(ctx, FR_HW_ALPHA_BLEND); // Alpha 混合加速
fr_compositor_set_hint(ctx, FR_HW_FILL_RECT);   // 矩形填充加速
```

---

## 集成指南

### 方式一：在 FunsOS 应用中使用 (原生)

这是最常见的使用方式。FunRender 作为 FunsOS 的一部分，天然可用：

```c
#include "funrender.h"    // FunRender 总头文件

int main(int argc, char **argv) {
    // 1. 初始化渲染引擎 (传入 FunsOS 帧缓冲)
    fr_handle_t ctx = fr_init(SCREEN_WIDTH, SCREEN_HEIGHT, framebuffer_ptr);

    // 2. 设置主题
    fr_set_theme(ctx, "default");

    // 3. 创建主窗口
    fr_handle_t window = fr_create_window(ctx, "My App", 800, 600);

    // 4. 添加控件
    fr_handle_t btn = fr_create_button(window, "Click Me!",
        (fr_rect_t){350, 250, 100, 40});
    fr_on_click(btn, my_click_handler, NULL);

    fr_handle_t label = fr_create_label(window, "Hello FunRender!",
        (fr_rect_t){320, 310, 160, 24});

    // 5. 主循环
    while (running) {
        fr_process_events(ctx);   // 处理输入事件
        fr_render(ctx);           // 渲染一帧
    }

    // 6. 清理
    fr_shutdown(ctx);
    return 0;
}
```

### 方式二：移植到其他平台

由于 FunRender 与内核解耦，理论上只需实现一个简单的图形后端适配层即可移植：

```c
/* 后端适配层接口 (需自行实现) */
typedef struct fr_backend {
    void (*put_pixel)(int x, int y, fr_color_t color);
    void (*fill_rect)(int x, int y, int w, int h, fr_color_t color);
    void (*draw_line)(int x0, int y0, int x1, int y1, fr_color_t color);
    void (*draw_text)(int x, int y, const char *text, fr_color_t color);
    fr_color_t (*get_pixel)(int x, int y);
    void (*blit)(int dx, int dy, void *src, int sw, int sh, int stride);
} fr_backend_t;

// 注册自定义后端
fr_handle_t fr_init_with_backend(int w, int h, fr_backend_t *backend);
```

可移植的目标平台示例：
- **SDL2** (跨平台): 用 `SDL_Renderer` 实现后端
- **HTML5 Canvas**: 用 WebAssembly + Canvas 2D API
- **Framebuffer** (Linux): 直接写 `/dev/fb0`
- **OpenGL**: 用 OpenGL 2D 纹理渲染

---

## 代码示例

### 示例 1: 创建带按钮的窗口

```c
#include "funrender.h"

static void on_ok_clicked(fr_handle_t widget, void *data) {
    printf("OK button pressed!\n");
}

static void on_cancel_clicked(fr_handle_t widget, void *data) {
    printf("Cancel button pressed!\n");
    fr_exit_main_loop();  // 退出应用
}

int main(int argc, char **argv) {
    /* 初始化 FunRender (假设 framebuffer 已由外部提供) */
    fr_handle_t ctx = fr_init(1024, 768, get_framebuffer());
    fr_set_theme(ctx, "default");

    /* 创建主窗口 */
    fr_handle_t win = fr_create_window(ctx, "FunRender Demo", 400, 300);

    /* 使用 VBox 垂直布局 */
    fr_layout_vbox(win, 10, 20);

    /* 添加标签 */
    fr_handle_t label = fr_create_label(win,
        "Welcome to FunRender Engine!",
        (fr_rect_t){0, 0, 380, 30});

    /* 添加 OK 按钮 */
    fr_handle_t btn_ok = fr_create_button(win, "OK",
        (fr_rect_t){120, 50, 80, 35});
    fr_on_click(btn_ok, on_ok_clicked, NULL);

    /* 添加 Cancel 按钮 */
    fr_handle_t btn_cancel = fr_create_button(win, "Cancel",
        (fr_rect_t){210, 50, 80, 35});
    fr_on_click(btn_cancel, on_cancel_clicked, NULL);

    /* 显示窗口并进入主循环 */
    fr_render(ctx);
    fr_main_loop(ctx);

    fr_shutdown(ctx);
    return 0;
}
```

### 示例 2: 主题化的 UI

```c
#include "funrender.h"

int main(int argc, char **argv) {
    fr_handle_t ctx = fr_init(800, 600, fb);

    /* 切换到暗色主题 */
    fr_set_theme(ctx, "dark");

    /* 创建窗口 */
    fr_handle_t win = fr_create_window(ctx, "Themed App", 600, 400);

    /* 添加各种控件以展示主题效果 */
    fr_create_label(win, "Dark Theme Active", (fr_rect_t){20, 20, 200, 24});
    fr_create_textbox(win, "Type here...", (fr_rect_t){20, 55, 300, 28});
    fr_create_checkbox(win, "Enable feature", 1, (fr_rect_t){20, 95, 200, 24});
    fr_create_slider(win, 0, 100, 50, (fr_rect_t){20, 135, 280, 24});
    fr_create_progress(win, 72, 100, (fr_rect_t){20, 175, 280, 20});
    fr_create_combobox(win, (fr_rect_t){20, 210, 200, 26});

    /* 添加一个亮色的按钮作为对比 */
    fr_handle_t bright_btn = fr_create_button(win, "Bright Accent!",
        (fr_rect_t){20, 255, 160, 36});
    fr_set_color(bright_btn, FR_COLOR_WHITE, FR_RGB(0, 120, 215));

    fr_main_loop(ctx);
    fr_shutdown(ctx);
    return 0;
}
```

### 示例 3: 带动画的控件

```c
#include "funrender.h"

static void on_fade_in_complete(fr_anim_t *anim, void *data) {
    printf("Fade-in animation complete!\n");
}

int main(int argc, char **argv) {
    fr_handle_t ctx = fr_init(640, 480, fb);
    fr_set_theme(ctx, "default");

    fr_handle_t win = fr_create_window(ctx, "Animation Demo", 500, 350);

    /* 创建一个初始透明的通知面板 */
    fr_handle_t panel = fr_create_container(win, (fr_rect_t){100, 100, 300, 150});
    fr_set_color(panel, FR_RGB(50, 50, 50), FR_RGB(230, 245, 255));
    /* 设置初始透明度为 0 (完全透明) */
    fr_set_opacity(panel, 0.0f);

    fr_create_label(panel, "New message received!", (fr_rect_t){20, 30, 260, 24});
    fr_handle_t dismiss_btn = fr_create_button(panel, "Dismiss",
        (fr_rect_t){110, 90, 80, 30});

    /* 淡入动画: 透明度 0 -> 1, 持续 500ms, 缓出立方 */
    fr_anim_t *fade_in = fr_animate_property(
        panel, "opacity",
        0.0f, 1.0f,
        500,
        FR_EASE_OUT_CUBIC
    );
    fr_on_animation_complete(fade_in, on_fade_in_complete, NULL);

    /* Dismiss 按钮点击时淡出 */
    fr_on_click(dismiss_btn, [](w, d){
        fr_animate_property(w->parent, "opacity", 1.0f, 0.0f, 300, FR_EASE_IN_CUBIC);
    }, NULL);

    fr_main_loop(ctx);
    fr_shutdown(ctx);
    return 0;
}
```

---

## 文件清单

以下是 `renderer/` 目录下的完整文件及其功能描述：

### 头文件 (`include/`)

| 文件 | 描述 |
|------|------|
| **`funrender.h`** | 总头文件 — 一键包含所有子模块；定义基础类型 (fr_handle_t, fr_rect_t, fr_color_t)；声明核心 API (fr_init, fr_shutdown, fr_render, fr_create_*, fr_set_*, fr_on_*) |
| **`fr_context.h`** | 渲染上下文类型定义 (fr_context_t)；帧缓冲管理接口；状态栈操作 |
| **`fr_widgets.h`** | 全部 30 种控件类型定义与常量；控件基类 `fr_widget_t` 及各控件扩展结构体 (fr_button_t, fr_label_t, fr_textbox_t ... fr_color_picker_t)；控件状态标志 |
| **`fr_widgets_extra.h`** | 扩展控件类型：增强进度条、增强工具栏、增强状态栏、标签容器、层级树视图、颜色选择器、MDI 区域 |
| **`fr_layout.h`** | 布局管理器类型与接口；HBox/VBox/Grid/Anchor 布局策略 |
| **`fr_theme.h`** | 主题系统类型 (fr_theme_t)；主题变量定义；主题切换/查询/预览/混合/导入导出 API |
| **`fr_animation.h`** | 动画系统类型 (fr_anim_t)；动画类型枚举；缓动函数枚举；动画控制 API |
| **`fr_events.h`** | 事件系统类型 (fr_event_t)；事件类型枚举；事件处理器类型定义；事件轮询/等待 API |
| **`fr_compositor.h`** | 合成器类型 (fr_compositor_t)；Alpha 混合模式枚举；脏区域跟踪 API；Z-order 管理接口 |
| **`fr_input.h`** | 输入系统类型；键盘/鼠标/触摸事件数据结构；输入设备抽象接口 |
| **`fr_particle.h`** | (v1.2 新增) 粒子系统类型；粒子发射器/力场/粒子类型定义 |
| **`fr_path.h`** | (v1.2 新增) 矢量路径类型；路径命令/填充规则/布尔运算接口 |
| **`fr_gradient.h`** | (v1.2 新增) 渐变类型；线性/径向/锥形/网格渐变定义与渲染 |
| **`fr_shape.h`** | (v1.2 新增) 预定义形状类型；圆角矩形/星形/多边形/箭头/气泡/拼图 |
| **`fr_texture.h`** | (v1.2 新增) 纹理管理；格式转换/Mipmap/图集/缓存/采样器接口 |
| **`fr_transform.h`** | 2D 仿射变换矩阵类型与操作接口 |

### 源文件 (`src/`)

| 文件 | 描述 |
|------|:--------:|
| **`context.c`** | 渲染上下文的初始化/销毁/帧管理；状态栈的 push/pop/save/restore |
| **`widgets.c`** | 最大文件 -- 全部 30+ 种控件的创建/属性设置/渲染实现/事件处理的统一入口 |
| **`widgets_extra.c`** | 扩展控件实现：增强进度条、增强工具栏、增强状态栏、标签容器、层级树视图、颜色选择器、MDI 区域 |
| **`layout.c`** | 四种布局算法的实现 (HBox/VBox/Grid/Anchor)；最小/最大尺寸计算 |
| **`theme.c`** | 主题加载/切换/查询；主题变量的应用；per-widget 主题覆盖 |
| **`animation.c`** | 动画引擎核心：动画插值器、缓动函数计算、动画帧更新、完成回调 |
| **`events.c`** | 事件队列管理；命中测试 (hit-test)；事件冒泡分发；焦点管理 |
| **`compositor.c`** | Z-order 排序与合成；7种混合模式; 双缓冲; 统计；脏区域收集与优化；窗口装饰绘制 |
| **`input.c`** | 输入设备数据采集；原始输入到标准事件的转换；输入状态追踪 |
| **`text.c`** | Unicode 文本测量；自动换行算法；光标位置计算；文本渲染 |
| **`canvas.c`** | 2D 画布绘图原语：像素/线/矩形/圆/多边形/渐变/图像 Blit |
| **`window.c`** | 顶级窗口生命周期管理；非客户区 (标题栏/边框) 绘制与事件处理；窗口拖拽/缩放 |
| **`effect.c`** | (v0.6 增强) 视觉效果引擎：高斯/径向/运动模糊；颜色调整(亮度/对比度/饱和度/色相/伽马)；阴影/内阴影/发光/描边效果 |
| **`transform.c`** | (v0.6 新增) 2D 仿射变换：平移/缩放/旋转/倾斜/镜像/矩阵组合/逆变换 |
| **`font_ext.c`** | (v0.6 新增) 扩展字体系统：度量/对齐/样式 (粗体/斜体/下划线/删除线/描边/阴影/发光/渐变) |
| **`gpu_bridge.c`** | (v0.6 新增) GPU 加速桥接：命令缓冲区/纹理管理/批量绘制/DMA 传输/同步/能力查询 |
| **`shader.c`** | (v0.6 新增) 着色器管线：程序生命周期管理；uniform系统(1f/2f/3f/4f/mat4/1i)；varying变量插值；5个内置着色器(纯色/颜色/纹理/Phong) |
| **`texture_manager.c`** | (v0.6 新增) 纹理管理器：128槽纹理池；LRU淘汰策略；Mipmap生成(2x2 box filter, 10级)；DXT1 BC1压缩解压 |
| **`clipboard.c`** | (v0.6 新增) 剪贴板集成：纯文本/富文本/图像/文件路径/多条目历史/格式协商 |
| **`particle.c`** | (v1.2 新增) 粒子系统：发射器/重力/风力/湍流/火花/烟雾/火焰/雨/雪效果 |
| **`path.c`** | (v1.2 新增) 矢量路径渲染：SVG 路径命令 (M/L/C/Q/A/Z)、描边、填充、布尔运算 |
| **`gradient.c`** | (v1.2 新增) 高级渐变：线性/径向/锥形/网格渐变、多色标、抖动渲染 |
| **`shape.c`** | (v1.2 新增) 形状库：圆角矩形/星形/多边形/箭头/气泡/拼图块 |
| **`texture.c`** | (v1.2 新增) 纹理管理：格式转换/Mipmap/纹理图集/纹理缓存/采样器 |

### 主题文件 (`themes/`)

| 文件 | 描述 |
|------|------|
| **`default.h`** | 默认蓝白主题 — 经典 Windows 风格，蓝色强调色，适度圆角与阴影 |
| **`dark.h`** | 暗色主题 — 深灰背景 (#1e1e1e)，浅色文字，柔和的强调色，护眼设计 |
| **`light.h`** | 亮色主题 — 纯白背景，扁平化设计，细边框，无阴影或极浅阴影 |
| **`ocean.h`** | (v1.2 新增) 海洋主题 — 深海蓝背景，青色强调，清爽专业风格 |
| **`forest.h`** | (v1.2 新增) 森林主题 — 自然绿色调，大地色背景，环保风格 |
| **`sunset.h`** | (v1.2 新增) 日落主题 — 暖色橙色强调，暖灰底色，大圆角温馨风格 |
| **`monochrome.h`** | (v1.2 新增) 高对比度 — 纯黑白，大字粗边框，无障碍辅助功能 |
| **`cyberpunk.h`** | (v1.2 新增) 赛博朋克 — 深黑背景，霓虹粉/青，等宽字体，科技风格 |
| **`retro.h`** | (v1.2 新增) 复古蒸汽波 — 粉彩柔和色调，大圆角，紫色强调，怀旧风格 |

---


## 许可证

```
MIT License

Copyright (c) 2025-2026 Funs Liu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

<p align="center">
  <strong>FunRender — 独立 UI 控件与渲染引擎</strong><br/>
  <em>FunRender — Independent UI Widget and Rendering Engine</em>
</p>