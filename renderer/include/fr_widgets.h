/* fr_widgets.h - 控件定义
 * 所有 UI 控件的类型和接口定义
 */

#ifndef FR_WIDGETS_H
#define FR_WIDGETS_H

#include "stdint.h"

/* 控件类型 */
#define FR_WIDGET_BUTTON     1
#define FR_WIDGET_LABEL      2
#define FR_WIDGET_TEXTBOX    3
#define FR_WIDGET_CHECKBOX   4
#define FR_WIDGET_RADIO      5
#define FR_WIDGET_SLIDER     6
#define FR_WIDGET_PROGRESS   7
#define FR_WIDGET_COMBOBOX   8
#define FR_WIDGET_LISTBOX    9
#define FR_WIDGET_TABLE      10
#define FR_WIDGET_TABVIEW    11
#define FR_WIDGET_MENU       12
#define FR_WIDGET_TOOLBAR    13
#define FR_WIDGET_STATUSBAR  14
#define FR_WIDGET_DIALOG     15
#define FR_WIDGET_SCROLLBAR  16
#define FR_WIDGET_TREEVIEW   17
#define FR_WIDGET_CONTAINER  18
#define FR_WIDGET_SPINBOX     19
#define FR_WIDGET_HSLIDER    20   /* 水平滑块 (与 SLIDER 区分方向) */
#define FR_WIDGET_VSLIDER    21   /* 垂直滑块 */
#define FR_WIDGET_PROGRESS2  22   /* 增强进度条 */
#define FR_WIDGET_TAB_CONTROL 23  /* 标签容器 */
#define FR_WIDGET_TREE_VIEW  24  /* 层级树形视图 */
#define FR_WIDGET_SPLITTER   25  /* 可拖动分割器 */
#define FR_WIDGET_TOOLBAR2   26  /* 增强工具栏 */
#define FR_WIDGET_STATUSBAR2 27  /* 增强状态栏 */
#define FR_WIDGET_MDI_AREA   28  /* 多文档界面区域 */
#define FR_WIDGET_CALENDAR   29  /* 日历日期选择器 */
#define FR_WIDGET_COLOR_PICKER 30 /* 颜色选择器 */

/* 控件状态标志 */
#define FR_STATE_VISIBLE    0x01
#define FR_STATE_ENABLED    0x02
#define FR_STATE_FOCUSED    0x04
#define FR_STATE_HOVERED    0x08
#define FR_STATE_PRESSED    0x10
#define FR_STATE_CHECKED    0x20
#define FR_STATE_DIRTY      0x40

/* 控件基类 */
typedef struct fr_widget {
    uint32_t type;              /* 控件类型 */
    uint32_t state;             /* 状态标志 */
    fr_rect_t bounds;           /* 位置和大小 */
    char text[256];             /* 文本内容 */

    fr_color_t fg_color;        /* 前景色 */
    fr_color_t bg_color;        /* 背景色 */

    struct fr_widget *parent;   /* 父控件 */
    struct fr_widget *first_child;
    struct fr_widget *next_sibling;

    /* 事件回调 */
    void (*on_click)(struct fr_widget *widget, void *data);
    void (*on_change)(struct fr_widget *widget, void *data);
    void (*on_key)(struct fr_widget *widget, void *data);
    void (*on_focus)(struct fr_widget *widget, int gained);
    void *user_data;

    /* 布局信息 */
    void *layout;
    int min_width, min_height;
    int max_width, max_height;

    /* 渲染函数 */
    void (*render)(struct fr_widget *widget, fr_context_t *ctx);
    void (*handle_event)(struct fr_widget *widget, void *event);
} fr_widget_t;

/* 按钮扩展 */
typedef struct {
    fr_widget_t base;
    int border_radius;
    fr_color_t hover_color;
    fr_color_t press_color;
} fr_button_t;

/* 标签扩展 */
typedef struct {
    fr_widget_t base;
    int wrap;           /* 自动换行 */
    int alignment;      /* 对齐方式: 0=左 1=中 2=右 */
} fr_label_t;

/* 文本框扩展 */
typedef struct {
    fr_widget_t base;
    int cursor_pos;
    int selection_start;
    int selection_end;
    int max_length;
    int password_mode;
    int multiline;
    int scroll_offset;
} fr_textbox_t;

/* 复选框扩展 */
typedef struct {
    fr_widget_t base;
    int checked;
} fr_checkbox_t;

/* 单选按钮扩展 */
typedef struct {
    fr_widget_t base;
    int selected;
    int group_id;
} fr_radio_t;

/* 滑块扩展 */
typedef struct {
    fr_widget_t base;
    int min_val;
    int max_val;
    int value;
    int orientation;    /* 0=水平, 1=垂直 */
    int tick_interval;
} fr_slider_t;

/* 进度条扩展 */
typedef struct {
    fr_widget_t base;
    int value;
    int max_val;
    int show_percent;
    fr_color_t fill_color;
} fr_progress_t;

/* 下拉框扩展 */
typedef struct {
    fr_widget_t base;
    char **items;
    int item_count;
    int selected_index;
    int dropdown_open;
} fr_combobox_t;

/* 列表框扩展 */
typedef struct {
    fr_widget_t base;
    char **items;
    int item_count;
    int selected_index;
    int top_item;
    int visible_items;
} fr_listbox_t;

/* 表格扩展 */
typedef struct {
    fr_widget_t base;
    int cols;
    int rows;
    char ***cells;          /* 二维字符串数组 */
    int *col_widths;
    int selected_row;
    int header_height;
} fr_table_t;

/* 标签页扩展 */
typedef struct {
    fr_widget_t base;
    char **tab_labels;
    int tab_count;
    int active_tab;
    fr_widget_t **tab_pages;
} fr_tabview_t;

/* 菜单扩展 */
typedef struct {
    fr_widget_t base;
    char **item_labels;
    int *item_types;        /* 0=普通, 1=分隔线, 2=子菜单 */
    int item_count;
    int selected_index;
    int visible;
} fr_menu_t;

/* 工具栏扩展 */
typedef struct {
    fr_widget_t base;
    fr_widget_t **buttons;
    int button_count;
    int orientation;
} fr_toolbar_t;

/* 状态栏扩展 */
typedef struct {
    fr_widget_t base;
    char sections[8][64];
    int section_widths[8];
    int section_count;
} fr_statusbar_t;

/* 对话框扩展 */
typedef struct {
    fr_widget_t base;
    int dialog_type;        /* 0=普通, 1=模态 */
    int result;             /* 0=无, 1=确定, 2=取消 */
    fr_widget_t *content;
} fr_dialog_t;

/* 滚动条扩展 */
typedef struct {
    fr_widget_t base;
    int orientation;        /* 0=水平, 1=垂直 */
    int min_val, max_val;
    int value;
    int page_size;
    int thumb_size;
    int thumb_pos;
} fr_scrollbar_t;

/* 树形控件扩展 */
typedef struct fr_tree_node {
    char text[128];
    int expanded;
    int level;
    struct fr_tree_node *first_child;
    struct fr_tree_node *next_sibling;
} fr_tree_node_t;

typedef struct {
    fr_widget_t base;
    fr_tree_node_t *root;
    int selected_node;
    int visible_depth;
} fr_treeview_t;

/* ---- 新增控件类型定义 ---- */

/* 数值微调框 (Spinbox) - 带加减按钮的数字输入 */
typedef struct {
    fr_widget_t base;
    int value;                  /* 当前值 */
    int min_val, max_val;       /* 范围 */
    int step;                   /* 每次增减步长 */
    int wrap;                   /* 是否循环 (超出范围时回绕) */
} fr_spinbox_t;

/* 水平/垂直滑块 (Slider) 扩展 */
typedef struct {
    fr_widget_t base;
    int min_val, max_val;
    int value;
    int orientation;            /* 0=水平, 1=垂直 */
    int tick_interval;          /* 刻度间隔 */
    int inverted;               /* 反向滑块 (右/下为小值) */
} fr_hslider_t;                /* 复用于 VSlider */

/* 增强进度条扩展 */
typedef struct {
    fr_widget_t base;
    int value;
    int max_val;
    int show_percent;           /* 显示百分比文字 */
    fr_color_t fill_color;
    fr_color_t bg_color;
    fr_color_t text_color;
    int animated;               /* 动画过渡效果 */
    float display_value;        /* 用于动画的显示值 */
} fr_progress2_t;

/* 标签容器 (Tab Control) 扩展 */
typedef struct {
    fr_widget_t base;
    char **tab_labels;          /* 标签名称数组 */
    uint32_t tab_count;         /* 标签数量 */
    uint32_t active_tab;        /* 当前活动标签索引 */
    fr_widget_t **tab_pages;    /* 每个标签对应的内容面板 */
    int closable;               /* 标签是否可关闭 */
    int tab_position;           /* 标签栏位置: 0=顶部, 1=底部, 2=左侧, 3=右侧 */
} fr_tab_control_t;

/* 层级树形视图 (Tree View) 扩展 */
typedef struct {
    fr_widget_t base;
    fr_tree_node_t *root;       /* 根节点 */
    int selected_node_idx;      /* 选中的节点索引 */
    int visible_depth;          /* 可见展开深度 */
    int show_lines;             /* 显示连接线 */
    int show_root_handles;      /* 显示根节点展开手柄 */
    int indent_px;              /* 缩进像素数 */
} fr_tree_view_t;

/* 分割器 (Splitter) 扩展 */
typedef struct {
    fr_widget_t base;
    int orientation;            /* 0=水平分割(左右), 1=垂直分割(上下) */
    int position;               /* 分割位置 (像素) */
    int min_pane_size;          /* 最小面板尺寸 */
    int dragging;               /* 正在拖动中 */
    int initial_pos;            /* 拖动起始位置 */
    fr_widget_t *pane_first;    /* 第一个面板 */
    fr_widget_t *pane_second;   /* 第二个面板 */
} fr_splitter_t;

/* 增强工具栏 (Toolbar2) 扩展 */
typedef struct {
    fr_widget_t base;
    fr_widget_t **buttons;      /* 工具按钮数组 */
    char **button_labels;       /* 按钮标签数组 */
    uint32_t button_count;
    int icon_size;              /* 图标大小 (像素) */
    int show_text;              /* 同时显示文字 */
    int orientation;            /* 0=水平, 1=垂直 */
    int separator_indices[16];  /* 分隔线位置 */
    uint32_t separator_count;
} fr_toolbar2_t;

/* 增强状态栏 (Statusbar2) 扩展 */
typedef struct {
    fr_widget_t base;
    char sections[8][64];       /* 各段文本内容 */
    int section_widths[8];      /* 各段宽度比例 */
    uint32_t section_count;
    fr_color_t section_colors[8]; /* 各段颜色 */
    int show_grip;              /* 显示拖动调整手柄 */
} fr_statusbar2_t;

/* 多文档界面区域 (MDI Area) 扩展 */
typedef struct {
    fr_widget_t base;
    fr_widget_t **children;     /* 子窗口列表 */
    uint32_t child_count;
    uint32_t max_children;      /* 最大子窗口数 */
    int active_child;           /* 当前活动子窗口索引 */
    int cascade_offset;         /* 级联排列偏移增量 */
    int arrangement;            /* 排列方式: 0=级联, 1=平铺, 2=水平, 3=垂直 */
} fr_mdi_area_t;

/* 日历控件 (Calendar) 扩展 */
typedef struct {
    fr_widget_t base;
    int year, month, day;       /* 当前选中日期 */
    int today_year, today_month, today_day;  /* 今天日期 */
    int sel_year, sel_month, sel_day;       /* 选择中的日期 (临时) */
    int header_visible;        /* 显示年月导航头 */
    int week_numbers;          /* 显示周数列 */
    int min_year, max_year;    /* 允许的年份范围 */
} fr_calendar_t;

/* 颜色选择器 (Color Picker) 扩展 */
typedef struct {
    fr_widget_t base;
    uint8_t r, g, b, a;        /* 当前选择的 RGBA 值 */
    int mode;                   /* 0=HSV色轮, 1=RGB滑块, 2=调色板 */
    int show_alpha;             /* 显示透明度通道 */
    int recent_count;           /* 最近使用颜色数量 */
    uint32_t recent_colors[12]; /* 最近使用的颜色列表 */
} fr_color_picker_t;

#endif /* FR_WIDGETS_H */
