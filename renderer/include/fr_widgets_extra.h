/* fr_widgets_extra.h - FUNSOS 渲染器扩展控件
 * 额外控件类型：树形视图、标签页、进度条、滑块、列表、滚动条等
 */

#ifndef FR_WIDGETS_EXTRA_H
#define FR_WIDGETS_EXTRA_H

#include "funrender.h"
#include "fr_context.h"

/* ---- 树形视图 ---- */

typedef struct fr_tree_node_ex {
    char        *label;          /* 节点标签 */
    void        *user_data;      /* 用户数据 */
    struct fr_tree_node_ex *parent; /* 父节点 */
    struct fr_tree_node_ex *first_child; /* 第一个子节点 */
    struct fr_tree_node_ex *next_sibling; /* 下一个兄弟节点 */
    struct fr_tree_node_ex *prev_sibling; /* 上一个兄弟节点 */
    uint32_t    child_count;     /* 子节点数量 */
    uint8_t     expanded;        /* 是否展开 */
    uint8_t     selected;        /* 是否选中 */
    uint8_t     has_children;    /* 是否有子节点 */
} fr_tree_node_ex_t;

typedef struct {
    fr_rect_t    bounds;         /* 控件边界 */
    fr_tree_node_ex_t *root;        /* 根节点 */
    fr_tree_node_ex_t *selected;    /* 当前选中节点 */
    uint32_t     scroll_offset;  /* 滚动偏移 */
    uint32_t     item_height;    /* 每项高度 */
    uint32_t     indent_width;   /* 缩进宽度 */
    uint8_t      show_lines;     /* 是否显示连线 */
    uint8_t      multi_select;   /* 是否允许多选 */
    uint8_t      visible;        /* 是否可见 */
    void (*on_select)(fr_tree_node_ex_t *node); /* 选择回调 */
} fr_tree_ex_t;

/* 创建树形视图 */
fr_tree_ex_t *fr_tree_create(fr_rect_t bounds, fr_tree_node_ex_t *root);

/* 销毁树形视图 */
void fr_tree_destroy(fr_tree_ex_t *tree);

/* 添加子节点 */
fr_tree_node_ex_t *fr_tree_add_node(fr_tree_node_ex_t *parent, const char *label, void *data);

/* 移除节点 */
void fr_tree_remove_node(fr_tree_node_ex_t *node);

/* 展开/折叠节点 */
void fr_tree_expand(fr_tree_node_ex_t *node);
void fr_tree_collapse(fr_tree_node_ex_t *node);

/* 选择节点 */
void fr_tree_select(fr_tree_ex_t *tree, fr_tree_node_ex_t *node);

/* 获取选中节点 */
fr_tree_node_ex_t *fr_tree_get_selected(fr_tree_ex_t *tree);

/* 渲染树形视图 */
void fr_tree_render(fr_tree_ex_t *tree, fr_context_t *ctx);

/* 处理树形视图事件 */
int fr_tree_handle_event(fr_tree_ex_t *tree, fr_event_t *event);

/* ---- 标签页 ---- */

#define FR_TAB_MAX_LABEL 32

typedef struct {
    char label[FR_TAB_MAX_LABEL]; /* 标签文本 */
    void *content;                /* 标签内容指针 */
    uint8_t active;               /* 是否为活动标签 */
    uint8_t closable;             /* 是否可关闭 */
} fr_tab_ex_t;

typedef struct {
    fr_rect_t    bounds;
    fr_tab_ex_t *tabs;           /* 标签数组 */
    uint32_t     tab_count;      /* 标签数量 */
    uint32_t     max_tabs;       /* 最大标签数 */
    uint32_t     active_index;   /* 当前活动标签索引 */
    uint32_t     tab_height;     /* 标签栏高度 */
    uint32_t     bg_color;       /* 背景色 */
    uint32_t     active_color;   /* 活动标签颜色 */
    uint32_t     inactive_color; /* 非活动标签颜色 */
    uint32_t     text_color;     /* 文字颜色 */
    uint8_t      visible;
    void (*on_tab_changed)(uint32_t index); /* 标签切换回调 */
} fr_tabcontrol_ex_t;

/* 创建标签页控件 */
fr_tabcontrol_ex_t *fr_tabcontrol_create(fr_rect_t bounds, uint32_t max_tabs);

/* 销毁标签页控件 */
void fr_tabcontrol_destroy(fr_tabcontrol_ex_t *tab);

/* 添加标签 */
int fr_tabcontrol_add_tab(fr_tabcontrol_ex_t *tab, const char *label, void *content, int closable);

/* 移除标签 */
int fr_tabcontrol_remove_tab(fr_tabcontrol_ex_t *tab, uint32_t index);

/* 切换标签 */
int fr_tabcontrol_set_active(fr_tabcontrol_ex_t *tab, uint32_t index);

/* 获取活动标签索引 */
uint32_t fr_tabcontrol_get_active(fr_tabcontrol_ex_t *tab);

/* 获取活动标签内容 */
void *fr_tabcontrol_get_content(fr_tabcontrol_ex_t *tab);

/* 渲染标签页 */
void fr_tabcontrol_render(fr_tabcontrol_ex_t *tab, fr_context_t *ctx);

/* 处理标签页事件 */
int fr_tabcontrol_handle_event(fr_tabcontrol_ex_t *tab, fr_event_t *event);

/* ---- 进度条 ---- */

#define FR_PROGRESS_HORIZONTAL 0
#define FR_PROGRESS_VERTICAL   1
#define FR_PROGRESS_MARQUEE    2  /* 不确定进度 */

typedef struct {
    fr_rect_t    bounds;
    uint32_t     min_value;     /* 最小值 */
    uint32_t     max_value;     /* 最大值 */
    uint32_t     current_value; /* 当前值 */
    uint32_t     orientation;   /* 方向 */
    uint32_t     bg_color;      /* 背景色 */
    uint32_t     fill_color;    /* 填充色 */
    uint32_t     border_color;  /* 边框色 */
    float        percent;       /* 百分比 (0.0-1.0) */
    uint8_t      show_percent;  /* 是否显示百分比文本 */
    uint8_t      visible;
} fr_progress_ex_t;

/* 创建进度条 */
fr_progress_ex_t *fr_progress_create(fr_rect_t bounds, uint32_t min_val, uint32_t max_val);

/* 销毁进度条 */
void fr_progress_destroy(fr_progress_ex_t *progress);

/* 设置进度值 */
void fr_progress_set_value(fr_progress_ex_t *progress, uint32_t value);

/* 获取进度值 */
uint32_t fr_progress_get_value(fr_progress_ex_t *progress);

/* 设置进度百分比 */
void fr_progress_set_percent(fr_progress_ex_t *progress, float percent);

/* 递增进度 */
void fr_progress_increment(fr_progress_ex_t *progress, uint32_t delta);

/* 渲染进度条 */
void fr_progress_render(fr_progress_ex_t *progress, fr_context_t *ctx);

/* ---- 滑块 ---- */

typedef struct {
    fr_rect_t    bounds;
    int32_t      min_value;
    int32_t      max_value;
    int32_t      current_value;
    int32_t      step;          /* 步进值 */
    uint32_t     orientation;   /* 方向 */
    uint32_t     track_height;  /* 轨道高度 */
    uint32_t     thumb_size;    /* 滑块大小 */
    uint32_t     track_color;
    uint32_t     fill_color;
    uint32_t     thumb_color;
    uint8_t      dragging;      /* 是否正在拖动 */
    uint8_t      visible;
    void (*on_value_changed)(int32_t value); /* 值变化回调 */
} fr_slider_ex_t;

/* 创建滑块 */
fr_slider_ex_t *fr_slider_create(fr_rect_t bounds, int32_t min_val, int32_t max_val, int32_t initial);

/* 销毁滑块 */
void fr_slider_destroy(fr_slider_ex_t *slider);

/* 设置值 */
void fr_slider_set_value(fr_slider_ex_t *slider, int32_t value);

/* 获取值 */
int32_t fr_slider_get_value(fr_slider_ex_t *slider);

/* 渲染滑块 */
void fr_slider_render(fr_slider_ex_t *slider, fr_context_t *ctx);

/* 处理滑块事件 */
int fr_slider_handle_event(fr_slider_ex_t *slider, fr_event_t *event);

/* ---- 滚动条 ---- */

typedef struct {
    fr_rect_t    bounds;
    uint32_t     total_size;    /* 内容总大小 */
    uint32_t     viewport_size; /* 可视区域大小 */
    uint32_t     scroll_pos;    /* 滚动位置 */
    uint32_t     orientation;   /* 方向 */
    uint32_t     thumb_size;    /* 滑块大小 */
    uint32_t     track_size;    /* 轨道大小 */
    uint32_t     track_color;
    uint32_t     thumb_color;
    uint32_t     arrow_color;
    uint8_t      dragging;
    uint8_t      visible;
    uint8_t      auto_hide;     /* 内容不足时自动隐藏 */
    void (*on_scroll)(uint32_t pos); /* 滚动回调 */
} fr_scrollbar_ex_t;

/* 创建滚动条 */
fr_scrollbar_ex_t *fr_scrollbar_create(fr_rect_t bounds, uint32_t orientation);

/* 销毁滚动条 */
void fr_scrollbar_destroy(fr_scrollbar_ex_t *scrollbar);

/* 设置范围 */
void fr_scrollbar_set_range(fr_scrollbar_ex_t *scrollbar, uint32_t total, uint32_t viewport);

/* 设置滚动位置 */
void fr_scrollbar_set_position(fr_scrollbar_ex_t *scrollbar, uint32_t pos);

/* 获取滚动位置 */
uint32_t fr_scrollbar_get_position(fr_scrollbar_ex_t *scrollbar);

/* 渲染滚动条 */
void fr_scrollbar_render(fr_scrollbar_ex_t *scrollbar, fr_context_t *ctx);

/* 处理滚动条事件 */
int fr_scrollbar_handle_event(fr_scrollbar_ex_t *scrollbar, fr_event_t *event);

/* ---- 分组框 ---- */

typedef struct {
    fr_rect_t    bounds;
    char         title[64];
    uint32_t     border_color;
    uint32_t     title_color;
    uint32_t     bg_color;
    uint8_t      visible;
    uint8_t      collapsible;
    uint8_t      collapsed;
} fr_groupbox_ex_t;

/* 创建分组框 */
fr_groupbox_ex_t *fr_groupbox_create(fr_rect_t bounds, const char *title);

/* 销毁分组框 */
void fr_groupbox_destroy(fr_groupbox_ex_t *group);

/* 渲染分组框 */
void fr_groupbox_render(fr_groupbox_ex_t *group, fr_context_t *ctx);

/* ---- 菜单 ---- */

#define FR_MENU_MAX_ITEMS 32
#define FR_MENU_MAX_LABEL 48

typedef struct fr_menu_item_ex {
    char         label[FR_MENU_MAX_LABEL];
    uint32_t     id;
    uint32_t     shortcut_key;
    uint32_t     shortcut_mods;
    uint8_t      enabled;
    uint8_t      checked;
    uint8_t      separator;       /* 是否为分隔线 */
    struct fr_menu_item_ex *submenu; /* 子菜单 */
    void (*on_click)(uint32_t id); /* 点击回调 */
} fr_menu_item_ex_t;

typedef struct {
    fr_rect_t    bounds;
    fr_menu_item_ex_t items[FR_MENU_MAX_ITEMS];
    uint32_t     item_count;
    uint32_t     hover_index;
    uint32_t     item_height;
    uint32_t     bg_color;
    uint32_t     hover_color;
    uint32_t     text_color;
    uint32_t     disabled_color;
    uint32_t     separator_color;
    uint8_t      visible;
    uint8_t      is_open;
} fr_menu_ex_t;

/* 创建菜单 */
fr_menu_ex_t *fr_menu_create(void);

/* 销毁菜单 */
void fr_menu_destroy(fr_menu_ex_t *menu);

/* 添加菜单项 */
int fr_menu_add_item(fr_menu_ex_t *menu, const char *label, uint32_t id,
                     void (*callback)(uint32_t id));

/* 添加分隔线 */
int fr_menu_add_separator(fr_menu_ex_t *menu);

/* 添加子菜单 */
int fr_menu_add_submenu(fr_menu_ex_t *menu, const char *label, fr_menu_ex_t *submenu);

/* 添加带快捷键的菜单项 */
int fr_menu_add_item_ex(fr_menu_ex_t *menu, const char *label, uint32_t id,
                        uint32_t key, uint32_t mods, void (*callback)(uint32_t id));

/* 显示菜单 */
void fr_menu_show(fr_menu_ex_t *menu, int x, int y);

/* 隐藏菜单 */
void fr_menu_hide(fr_menu_ex_t *menu);

/* 渲染菜单 */
void fr_menu_render(fr_menu_ex_t *menu, fr_context_t *ctx);

/* 处理菜单事件 */
int fr_menu_handle_event(fr_menu_ex_t *menu, fr_event_t *event);

/* 启用/禁用菜单项 */
void fr_menu_set_enabled(fr_menu_ex_t *menu, uint32_t index, int enabled);

/* 设置菜单项选中状态 */
void fr_menu_set_checked(fr_menu_ex_t *menu, uint32_t index, int checked);

#endif /* FR_WIDGETS_EXTRA_H */