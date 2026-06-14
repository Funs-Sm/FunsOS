/* funrender.h - FUNRender 渲染引擎总头文件
 * 包含所有渲染引擎子模块，提供统一的 UI 渲染 API
 */

#ifndef FUNRENDER_H
#define FUNRENDER_H

#include "stdint.h"
#include "stddef.h"

/* ---- 基础类型 ---- */
typedef void *fr_handle_t;

typedef struct { float x, y, w, h; } fr_rect_t;

#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 颜色辅助宏 */
#define FR_COLOR(r, g, b, a)  ((fr_color_t){(r), (g), (b), (a)})
#define FR_RGB(r, g, b)       FR_COLOR(r, g, b, 255)
#define FR_RGBA(r, g, b, a)   FR_COLOR(r, g, b, a)

/* 预定义颜色 */
#define FR_COLOR_BLACK    FR_RGB(0, 0, 0)
#define FR_COLOR_WHITE    FR_RGB(255, 255, 255)
#define FR_COLOR_RED      FR_RGB(255, 0, 0)
#define FR_COLOR_GREEN    FR_RGB(0, 255, 0)
#define FR_COLOR_BLUE     FR_RGB(0, 0, 255)
#define FR_COLOR_GRAY     FR_RGB(128, 128, 128)
#define FR_COLOR_LIGHT_GRAY FR_RGB(192, 192, 192)
#define FR_COLOR_DARK_GRAY  FR_RGB(64, 64, 64)
#define FR_COLOR_TRANSPARENT FR_RGBA(0, 0, 0, 0)

/* ---- 初始化/销毁 ---- */
fr_handle_t fr_init(int width, int height, void *framebuffer);
void fr_shutdown(fr_handle_t ctx);

/* ---- 控件创建 ---- */
fr_handle_t fr_create_button(fr_handle_t parent, const char *text, fr_rect_t bounds);
fr_handle_t fr_create_label(fr_handle_t parent, const char *text, fr_rect_t bounds);
fr_handle_t fr_create_textbox(fr_handle_t parent, const char *text, fr_rect_t bounds);
fr_handle_t fr_create_checkbox(fr_handle_t parent, const char *text, int checked, fr_rect_t bounds);
fr_handle_t fr_create_slider(fr_handle_t parent, int min, int max, int value, fr_rect_t bounds);
fr_handle_t fr_create_progress(fr_handle_t parent, int value, int max, fr_rect_t bounds);
fr_handle_t fr_create_combobox(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_listbox(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_table(fr_handle_t parent, int cols, int rows, fr_rect_t bounds);
fr_handle_t fr_create_tabview(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_menu(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_toolbar(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_statusbar(fr_handle_t parent, fr_rect_t bounds);
fr_handle_t fr_create_dialog(fr_handle_t parent, const char *title, fr_rect_t bounds);
fr_handle_t fr_create_scrollbar(fr_handle_t parent, int orient, fr_rect_t bounds);

/* ---- 控件操作 ---- */
void fr_set_text(fr_handle_t widget, const char *text);
const char *fr_get_text(fr_handle_t widget);
void fr_set_color(fr_handle_t widget, fr_color_t fg, fr_color_t bg);
void fr_set_visible(fr_handle_t widget, int visible);
void fr_set_enabled(fr_handle_t widget, int enabled);
void fr_destroy_widget(fr_handle_t widget);

/* ---- 布局 ---- */
fr_handle_t fr_layout_hbox(fr_handle_t parent, int spacing, int margin);
fr_handle_t fr_layout_vbox(fr_handle_t parent, int spacing, int margin);
fr_handle_t fr_layout_grid(fr_handle_t parent, int cols, int rows, int spacing);
fr_handle_t fr_layout_anchor(fr_handle_t parent, int anchor_flags);

/* ---- 渲染 ---- */
void fr_render(fr_handle_t ctx);
void fr_invalidate(fr_handle_t widget);
void fr_invalidate_all(fr_handle_t ctx);

/* ---- 事件 ---- */
typedef void (*fr_event_handler)(fr_handle_t widget, int event_type, void *event_data);

void fr_on_click(fr_handle_t widget, fr_event_handler handler);
void fr_on_change(fr_handle_t widget, fr_event_handler handler);
void fr_on_key(fr_handle_t widget, fr_event_handler handler);
void fr_process_events(fr_handle_t ctx);

/* ---- 主题 ---- */
void fr_set_theme(fr_handle_t ctx, const char *theme_name);
void fr_set_font(fr_handle_t ctx, const char *font_name, int size);

/* 包含子模块 */
#include "fr_context.h"
#include "fr_widgets.h"
#include "fr_layout.h"
#include "fr_theme.h"
#include "fr_animation.h"
#include "fr_input.h"
#include "fr_events.h"

/* 扩展模块 */
#include "fr_effect.h"
#include "fr_transform.h"
#include "fr_font_ext.h"
#include "fr_gpu.h"
#include "fr_clipboard.h"
#include "fr_widgets_extra.h"

/* 新增模块 */
#include "fr_particle.h"
#include "fr_path.h"
#include "fr_gradient.h"
#include "fr_shape.h"
#include "fr_texture.h"

#endif /* FUNRENDER_H */
