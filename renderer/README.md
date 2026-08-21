# FunRender - 独立 UI 渲染引擎

<p align="center">
  <img src="https://img.shields.io/badge/FunRender-v1.3-blue" alt="Version"/>
  <img src="https://img.shields.io/badge/Status-Stable-green" alt="Status"/>
  <img src="https://img.shields.io/badge/Widgets-30%2B-purple" alt="Widget Count"/>
  <img src="https://img.shields.io/badge/Themes-9-yellow" alt="Theme Count"/>
  <img src="https://img.shields.io/badge/Modules-30-orange" alt="Module Count"/>
</p>

<p align="center">
  <strong>模块化 UI 控件与渲染引擎</strong><br/>
  <em>A modular UI widget and rendering engine</em>
</p>

---

**Copyright (c) 2025-2026 Funs Liu. Licensed under the MIT License.**

---

## 简介

**FunRender** 是 FunsOS 操作系统中的 UI 控件渲染与管理引擎。它与 FunsOS 内核完全解耦——不编译进内核映像，而是作为独立的用户态/上层库存在，通过 `gui/gfx.h` 和 `gui/gfx3d.h` 提供的底层图形 API 进行像素级绘制。

### 内核独立性优势

| 优势 | 说明 |
|------|------|
| **安全性** | UI 渲染漏洞不会危及内核稳定性 |
| **可维护性** | UI 引擎可独立升级，无需重新编译内核 |
| **复用性** | 理论上可将 FunRender 移植到其他图形后端或操作系统 |
| **可测试性** | 可在宿主机上进行单元测试和可视化预览 |

### 核心能力

- **30+ 种 UI 控件** — 覆盖桌面应用的所有常见需求
- **9 套内置主题** — Default / Dark / Light / Ocean / Forest / Sunset / Monochrome / Cyberpunk / Retro
- **灵活的布局系统** — HBox / VBox / Grid / Anchor / Flex
- **动画引擎** — 淡入淡出、滑动、缩放、旋转、弹性动画
- **完整的窗口管理** — Z-order、焦点管理、拖拽、缩放
- **合成器** — Alpha 混合、脏区域优化、硬件加速提示

---

## 设计理念

### 核心设计原则

| 原则 | 说明 |
|------|------|
| **模块化 (Modularity)** | 每个功能域是独立模块（widget/layout/theme/animation...），可单独替换 |
| **数据驱动 (Data-Driven)** | 控件属性、主题配色、动画曲线均由数据结构定义，便于序列化和热更新 |
| **回调驱动 (Callback-Driven)** | 事件处理采用函数指针回调模式，解耦事件源与业务逻辑 |
| **组合优于继承 (Composition over Inheritance)** | 控件通过父子关系树组织，而非深层继承体系 |
| **即时模式与保留模式结合** | 布局计算使用保留模式（控件树持久存在），渲染可采用脏区域优化的即时模式 |

### 与内核集成 UI 的对比

| 传统内核集成方式 | FunRender 独立方式 |
|-----------------|-------------------|
| UI 代码编译进内核，增大内核体积 | 独立库，按需加载 |
| UI Bug 可能导致内核崩溃 | UI 异常仅影响应用进程 |
| 修改 UI 需要重新编译整个内核 | 独立修改、独立部署 |
| 无法在开发机上预览 UI | 可在宿主机进行 UI 开发与调试 |
| 内核态图形调用，上下文切换开销大 | 用户态渲染，更高效的开发体验 |

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
│         window.c — 窗口创建/销毁/标题栏/边框/阴影/Z-order    │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               合成器层 (Compositor)                           │
│   compositor.c — Z-order排序/Alpha混合/脏区域跟踪/HW加速     │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               控件层 (Widget Layer)                           │
│   widgets.c — 30+ 种控件的创建/属性/渲染/事件处理            │
│   ┌────────┬────────┬────────┬────────┬────────┬───┐      │
│   │ Button │ Label  │Textbox │Checkbox│ Slider │...│ 30+  │
│   └────────┴────────┴────────┴────────┴────────┴───┘        │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               布局层 (Layout Layer)                           │
│   layout.c — HBox/VBox/Grid/Anchor/Flex 弹性布局           │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               主题层 (Theme Layer)                            │
│   theme.c + themes/ — 颜色/字体/圆角/阴影/间距 定义          │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               动画层 (Animation Layer)                        │
│   animation.c — 淡入淡出/滑动/缩放/旋转/弹跳 + 缓动函数      │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               事件层 (Event Layer)                            │
│   events.c — 键盘/鼠标/触摸/手势/焦点的分发与冒泡            │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               输入层 (Input Layer)                            │
│   input.c — 原始输入设备数据的采集与标准化                    │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               文本/画布层 (Text & Canvas Layer)               │
│   text.c + canvas.c — 文本测量/渲染/换行 + 2D 画布绘图原语   │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│               上下文层 (Context Layer)                        │
│   context.c — 渲染上下文初始化/帧缓冲管理/状态栈              │
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

### 1. 上下文层 (Context)

渲染上下文是 FunRender 的根基，管理整个渲染生命周期的全局状态。

| 功能 | 描述 |
|------|------|
| `fr_init(width, height, fb)` | 初始化渲染引擎，绑定帧缓冲区 |
| `fr_shutdown(ctx)` | 销毁渲染上下文，释放所有资源 |
| `fr_begin_frame(ctx)` | 开始新一帧渲染 (清除脏标记) |
| `fr_end_frame(ctx)` | 结束当前帧 (提交到帧缓冲) |
| 状态栈 | 保存/恢复裁剪区域、变换矩阵、颜色状态 |

### 2. 控件层 (Widgets)

FunRender 的核心——**30 种 UI 控件类型**，每种控件都有统一的基类 (`fr_widget_t`) 和扩展属性。

| # | 控件类型 | 类型常量 | 描述 |
|:-:|----------|----------|------|
| 1 | **Button** | `FR_WIDGET_BUTTON` | 可点击的命令触发控件 |
| 2 | **Label** | `FR_WIDGET_LABEL` | 纯文本显示控件 |
| 3 | **Textbox** | `FR_WIDGET_TEXTBOX` | 单行/多行文本输入 |
| 4 | **Checkbox** | `FR_WIDGET_CHECKBOX` | 勾选/取消勾选 |
| 5 | **Radio** | `FR_WIDGET_RADIO` | 互斥选择的单选组 |
| 6 | **Slider** | `FR_WIDGET_SLIDER` | 数值范围滑动选择 |
| 7 | **Progress** | `FR_WIDGET_PROGRESS` | 任务进度百分比指示 |
| 8 | **Combobox** | `FR_WIDGET_COMBOBOX` | 下拉选择列表 |
| 9 | **Listbox** | `FR_WIDGET_LISTBOX` | 可滚动列表选择 |
| 10 | **Table** | `FR_WIDGET_TABLE` | 行列式数据表格 |
| 11 | **TabView** | `FR_WIDGET_TABVIEW` | 多页面标签切换 |
| 12 | **Menu** | `FR_WIDGET_MENU` | 下拉/弹出菜单 |
| 13 | **Toolbar** | `FR_WIDGET_TOOLBAR` | 按钮工具条 |
| 14 | **Statusbar** | `FR_WIDGET_STATUSBAR` | 底部状态信息栏 |
| 15 | **Dialog** | `FR_WIDGET_DIALOG` | 模态/非模态对话框 |
| 16 | **Scrollbar** | `FR_WIDGET_SCROLLBAR` | 滚动位置指示器 |
| 17 | **Treeview** | `FR_WIDGET_TREEVIEW` | 层级数据展开/折叠 |
| 18 | **Container** | `FR_WIDGET_CONTAINER` | 通用子控件容器 |
| 19 | **Spinbox** | `FR_WIDGET_SPINBOX` | 带加减按钮的数字输入 |
| 20 | **Splitter** | `FR_WIDGET_SPLITTER` | 可拖动的面板分割线 |
| 21 | **Calendar** | `FR_WIDGET_CALENDAR` | 日期选择器控件 |
| 22 | **Color Picker** | `FR_WIDGET_COLOR_PICKER` | RGBA 颜色选择 |
| 23+ | **更多控件** | - | 扩展控件见 `fr_widgets_extra.h` |

### 3. 布局层 (Layout)

自动计算子控件的位置和大小。

| 布局类型 | 函数 | 描述 |
|----------|------|------|
| **水平布局 (HBox)** | `fr_layout_hbox(parent, spacing, margin)` | 子控件水平排列，支持均匀分布 |
| **垂直布局 (VBox)** | `fr_layout_vbox(parent, spacing, margin)` | 子控件垂直排列 |
| **网格布局 (Grid)** | `fr_layout_grid(parent, cols, rows, spacing)` | 二维网格排列 |
| **锚点布局 (Anchor)** | `fr_layout_anchor(parent, anchor_flags)` | 相对于父容器边缘锚定 |
| **弹性布局 (Flex)** | `fr_layout_flex(parent, direction, wrap)` | CSS Flexbox 风格布局 |

### 4. 主题层 (Theme)

全局视觉风格管理系统。FunRender 内置 **9 套主题**：

| 主题名称 | 文件 | 描述 | 适用场景 |
|----------|------|------|----------|
| **Default** | `themes/default.h` | 蓝色调经典主题，类似 Windows 7 Aero | 日常使用、默认首选 |
| **Dark** | `themes/dark.h` | 深灰/近黑背景，护眼暗色主题 | 夜间使用、开发者偏好 |
| **Light** | `themes/light.h` | 白色明亮主题，极简扁平风格 | 日间使用、简约美学 |
| **Ocean** | `themes/ocean.h` | 深海蓝背景，青色强调色调 | 清爽专业风格 |
| **Forest** | `themes/forest.h` | 自然绿色调，大地色背景 | 环保/健康类应用 |
| **Sunset** | `themes/sunset.h` | 暖色橙色强调，暖灰底色，大圆角 | 温馨舒适风格 |
| **Monochrome** | `themes/monochrome.h` | 纯黑白高对比度，大字粗边框 | 无障碍辅助功能 |
| **Cyberpunk** | `themes/cyberpunk.h` | 深黑背景，霓虹粉/青强调，等宽字体 | 开发者/科技风格 |
| **Retro** | `themes/retro.h` | 粉彩柔和色调，大圆角，紫色强调 | 怀旧/创意风格 |

### 5. 动画层 (Animation)

为控件属性变化提供平滑过渡效果。

| 动画类型 | 描述 | 典型用途 |
|----------|------|----------|
| **Fade** | 透明度从 0->1 或 1->0 过渡 | 窗口出现/消失、提示框 |
| **Slide** | 控件从一个位置滑入另一个位置 | 侧边栏展开/收起、菜单弹出 |
| **Scale** | 控件从小到大或从大到小变化 | 对话框弹出、按钮点击反馈 |
| **Rotate** | 控件绕中心轴旋转 | 加载动画、特效 |
| **Bounce** | 控件带弹性阻尼运动到达目标位置 | 通知弹出、趣味交互 |

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

### 6. 事件层 (Events)

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

### 7. 合成器 (Compositor)

将多个窗口/图层合成为最终帧缓冲图像。

| 能力 | 描述 |
|------|------|
| **Z-Order 排序** | 按深度排序窗口，正确处理遮挡关系 |
| **Alpha 混合** | 支持正常/叠加/正片叠底等多种混合模式 |
| **脏区域跟踪** | 仅重绘发生变化的区域，大幅提升性能 |
| **硬件加速提示** | 标记可通过 GPU 加速的操作 |
| **窗口装饰** | 自动绘制标题栏、边框、阴影、圆角 |
| **光标合成** | 将鼠标光标合成到最终画面顶层 |

### 8. 输入层 (Input)

原始输入设备的抽象层，将不同来源的输入统一为标准事件格式。

| 设备类型 | 支持情况 |
|----------|----------|
| **键盘** | 完整支持：按下/释放/重复按键；修饰键；快捷键组合 |
| **鼠标** | 完整支持：移动/按下/释放/双击；左/中/右键；滚轮 |
| **触摸** | 计划中：单指/多点触控；手势识别 |
| **游戏手柄** | 计划中：按键/摇杆/扳机/震动反馈 |

### 9. 文本层 (Text)

Unicode 文本的测量、换行、光标定位与渲染。

### 10. 画布层 (Canvas)

2D 绘图原语的直接接口，供高级控件内部使用。

---

## 文件清单

### 头文件 (`include/`)

| 文件 | 描述 |
|------|------|
| `funrender.h` | 总头文件 — 一键包含所有子模块；定义基础类型 (fr_handle_t, fr_rect_t, fr_color_t)；声明核心 API |
| `fr_context.h` | 渲染上下文类型定义；帧缓冲管理接口；状态栈操作 |
| `fr_widgets.h` | 全部 30 种控件类型定义与常量；控件基类 `fr_widget_t` 及各控件扩展结构体 |
| `fr_widgets_extra.h` | 扩展控件类型：增强进度条、增强工具栏、增强状态栏、标签容器等 |
| `fr_layout.h` | 布局管理器类型与接口；HBox/VBox/Grid/Anchor/Flex 布局策略 |
| `fr_theme.h` | 主题系统类型；主题变量定义；主题切换/查询/预览/混合/导入导出 API |
| `fr_animation.h` | 动画系统类型；动画类型枚举；缓动函数枚举；动画控制 API |
| `fr_events.h` | 事件系统类型；事件类型枚举；事件处理器类型定义；事件轮询/等待 API |
| `fr_compositor.h` | 合成器类型；Alpha 混合模式枚举；脏区域跟踪 API；Z-order 管理接口 |
| `fr_input.h` | 输入系统类型；键盘/鼠标/触摸事件数据结构；输入设备抽象接口 |
| `fr_effect.h` | 视觉效果类型；模糊/阴影/发光/描边效果定义与渲染 |
| `fr_transform.h` | 2D 仿射变换矩阵类型与操作接口 |
| `fr_font_ext.h` | 扩展字体系统；度量/对齐/样式 (粗体/斜体/下划线...) |
| `fr_texture.h` | 纹理管理；格式转换/Mipmap/图集/缓存/采样器接口 |
| `fr_particle.h` | 粒子系统类型；粒子发射器/力场/粒子类型定义 |
| `fr_path.h` | 矢量路径类型；路径命令/填充规则/布尔运算接口 |
| `fr_gradient.h` | 渐变类型；线性/径向/锥形/网格渐变定义与渲染 |
| `fr_shape.h` | 预定义形状类型；圆角矩形/星形/多边形/箭头/气泡 |
| `fr_clipboard.h` | 剪贴板集成；纯文本/富文本/图像/文件路径 |
| `fr_css_ui.h` | CSS 风格 UI 解析；样式规则解析与应用到控件 |
| `fr_gpu.h` | GPU 加速接口；命令缓冲区/纹理管理/批量绘制 |

### 源文件 (`src/`)

| 文件 | 描述 |
|------|:--------:|
| `context.c` | 渲染上下文的初始化/销毁/帧管理；状态栈的 push/pop/save/restore |
| `widgets.c` | 全部 30+ 种控件的创建/属性设置/渲染实现/事件处理 |
| `widgets_extra.c` | 扩展控件实现：增强进度条、增强工具栏、增强状态栏等 |
| `layout.c` | 五种布局算法的实现 (HBox/VBox/Grid/Anchor/Flex)；最小/最大尺寸计算 |
| `theme.c` | 主题加载/切换/查询；主题变量的应用；per-widget 主题覆盖 |
| `animation.c` | 动画引擎核心：动画插值器、缓动函数计算、动画帧更新、完成回调 |
| `events.c` | 事件队列管理；命中测试 (hit-test)；事件冒泡分发；焦点管理 |
| `compositor.c` | Z-order 排序与合成；Alpha 混合；双缓冲；统计；脏区域收集与优化；窗口装饰绘制 |
| `input.c` | 输入设备数据采集；原始输入到标准事件的转换；输入状态追踪 |
| `text.c` | Unicode 文本测量；自动换行算法；光标位置计算；文本渲染 |
| `canvas.c` | 2D 画布绘图原语：像素/线/矩形/圆/多边形/渐变/图像 Blit |
| `window.c` | 顶级窗口生命周期管理；非客户区 (标题栏/边框) 绘制与事件处理；窗口拖拽/缩放 |
| `effect.c` | 视觉效果引擎：高斯/径向/运动模糊；颜色调整(亮度/对比度/饱和度/色相/伽马)；阴影/内阴影/发光/描边效果 |
| `effect_s8.c` | 控件透明度效果 |
| `transform.c` | 2D 仿射变换：平移/缩放/旋转/倾斜/镜像/矩阵组合/逆变换 |
| `font_ext.c` | 扩展字体系统：度量/对齐/样式 (粗体/斜体/下划线/删除线/描边/阴影/发光/渐变) |
| `texture.c` | 纹理管理：格式转换/Mipmap/纹理图集/纹理缓存/采样器 |
| `texture_manager.c` | 纹理池管理：128槽纹理池；LRU淘汰策略；Mipmap生成；DXT1 BC1压缩 |
| `gpu_bridge.c` | GPU 加速桥接：命令缓冲区/纹理管理/批量绘制/DMA 传输/同步/能力查询 |
| `shader.c` | 着色器管线：程序生命周期管理；uniform系统；varying变量插值；内置着色器 |
| `particle.c` | 粒子系统：发射器/重力/风力/湍流/火花/烟雾/火焰/雨/雪效果 |
| `path.c` | 矢量路径渲染：SVG 路径命令 (M/L/C/Q/A/Z)、描边、填充、布尔运算 |
| `gradient.c` | 高级渐变：线性/径向/锥形/网格渐变、多色标、抖动渲染 |
| `shape.c` | 形状库：圆角矩形/星形/多边形/箭头/气泡/拼图块 |
| `clipboard.c` | 剪贴板集成：纯文本/富文本/图像/文件路径/多条目历史/格式协商 |
| `css_ui.c` | CSS 风格 UI 解析器：样式规则解析、选择器匹配、属性应用到控件 |
| `primitive.c` | 图元绘制：像素级绘制原语 |
| `math_util.c` | 数学工具函数：向量/矩阵运算、几何计算 |

### 主题文件 (`themes/`)

| 文件 | 描述 |
|------|------|
| `default.h` | 默认蓝白主题 — 经典 Windows 风格，蓝色强调色，适度圆角与阴影 |
| `dark.h` | 暗色主题 — 深灰背景 (#1e1e1e)，浅色文字，柔和的强调色，护眼设计 |
| `light.h` | 亮色主题 — 纯白背景，扁平化设计，细边框，无阴影或极浅阴影 |
| `ocean.h` | 海洋主题 — 深海蓝背景，青色强调，清爽专业风格 |
| `forest.h` | 森林主题 — 自然绿色调，大地色背景，环保风格 |
| `sunset.h` | 日落主题 — 暖色橙色强调，暖灰底色，大圆角温馨风格 |
| `monochrome.h` | 高对比度 — 纯黑白，大字粗边框，无障碍辅助功能 |
| `cyberpunk.h` | 赛博朋克 — 深黑背景，霓虹粉/青，等宽字体，科技风格 |
| `retro.h` | 复古蒸汽波 — 粉彩柔和色调，大圆角，紫色强调，怀旧风格 |

---

## 集成指南

### 在 FunsOS 应用中使用

```c
#include "funrender.h"

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

### 移植到其他平台

只需实现一个简单的图形后端适配层：

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
    fr_on_click(dismiss_btn, on_dismiss_clicked, panel);

    fr_main_loop(ctx);
    fr_shutdown(ctx);
    return 0;
}
```

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
