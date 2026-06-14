/* widgets.c - 控件实现
 * 所有 UI 控件的创建、渲染和交互逻辑
 */

#include "funrender.h"
#include "fr_context.h"
#include "gfx.h"
#include "string.h"

/* ---- 辅助函数 ---- */

/* 分配控件内存 */
static fr_widget_t *widget_alloc(uint32_t type, int extra_size)
{
    fr_widget_t *w = (fr_widget_t *)fr_alloc(sizeof(fr_widget_t) + extra_size);
    if (w == NULL) return NULL;

    w->type = type;
    w->state = FR_STATE_VISIBLE | FR_STATE_ENABLED;
    w->bounds.x = 0; w->bounds.y = 0; w->bounds.w = 0; w->bounds.h = 0;
    w->text[0] = '\0';
    w->fg_color = FR_COLOR_BLACK;
    w->bg_color = FR_COLOR_WHITE;
    w->parent = NULL;
    w->first_child = NULL;
    w->next_sibling = NULL;
    w->on_click = NULL;
    w->on_change = NULL;
    w->on_key = NULL;
    w->on_focus = NULL;
    w->user_data = NULL;
    w->layout = NULL;
    w->min_width = 0; w->min_height = 0;
    w->max_width = 0x7FFFFFFF; w->max_height = 0x7FFFFFFF;
    w->render = NULL;
    w->handle_event = NULL;

    return w;
}

/* 将控件添加到父控件 */
static void widget_add_child(fr_widget_t *parent, fr_widget_t *child)
{
    if (parent == NULL || child == NULL) return;
    child->parent = parent;

    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        fr_widget_t *sibling = parent->first_child;
        while (sibling->next_sibling != NULL)
            sibling = sibling->next_sibling;
        sibling->next_sibling = child;
    }
}

/* ---- 按钮渲染 ---- */
static void button_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;
    fr_button_t *btn = (fr_button_t *)widget;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    /* 选择背景色 */
    fr_color_t bg = widget->bg_color;
    if (!(widget->state & FR_STATE_ENABLED)) {
        bg = FR_COLOR_LIGHT_GRAY;
    } else if (widget->state & FR_STATE_PRESSED) {
        bg = btn->press_color.r || btn->press_color.g || btn->press_color.b ?
             btn->press_color : FR_COLOR(180, 180, 180, 255);
    } else if (widget->state & FR_STATE_HOVERED) {
        bg = btn->hover_color.r || btn->hover_color.g || btn->hover_color.b ?
             btn->hover_color : FR_COLOR(220, 220, 220, 255);
    }

    /* 绘制圆角矩形背景 */
    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rounded_rect(&gfx_ctx, rect, btn->border_radius > 0 ? btn->border_radius : 4,
                          (bg.r << 16) | (bg.g << 8) | bg.b);

    /* 绘制边框 */
    gfx_draw_rounded_rect(&gfx_ctx, rect, btn->border_radius > 0 ? btn->border_radius : 4,
                          0x808080);

    /* 绘制文字 */
    if (widget->text[0]) {
        /* 居中绘制 */
        int text_x = x + w / 2 - (int)(widget->text[0] ? 4 : 0) * 3;
        int text_y = y + h / 2 - 6;
        uint32_t text_color = (widget->fg_color.r << 16) |
                              (widget->fg_color.g << 8) | widget->fg_color.b;
        gfx_point_t pt = {text_x, text_y};
        /* 使用 gfx 绘制文字 - 简化实现 */
        (void)pt;
        (void)text_color;
    }
}

/* ---- 标签渲染 ---- */
static void label_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;

    /* 绘制文字 */
    if (widget->text[0]) {
        uint32_t text_color = (widget->fg_color.r << 16) |
                              (widget->fg_color.g << 8) | widget->fg_color.b;
        (void)x; (void)y; (void)text_color;
        /* 实际绘制由字体引擎完成 */
    }
}

/* ---- 文本框渲染 ---- */
static void textbox_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 白色背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xFFFFFF);

    /* 边框 */
    uint32_t border_color = (widget->state & FR_STATE_FOCUSED) ? 0x0078D4 : 0x808080;
    gfx_draw_rect(&gfx_ctx, rect, border_color);

    /* 绘制文字 */
    if (widget->text[0]) {
        (void)border_color;
    }

    /* 绘制光标 */
    if (widget->state & FR_STATE_FOCUSED) {
        fr_textbox_t *tb = (fr_textbox_t *)widget;
        int cursor_x = x + 4 + tb->cursor_pos * 8;
        gfx_rect_t cursor_rect = {cursor_x, y + 4, 2, h - 8};
        gfx_fill_rect(&gfx_ctx, cursor_rect, 0x000000);
    }
}

/* ---- 复选框渲染 ---- */
static void checkbox_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 绘制方框 */
    gfx_rect_t box = {x, y + 2, 16, 16};
    gfx_fill_rect(&gfx_ctx, box, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, box, 0x808080);

    /* 如果选中，绘制勾号 */
    fr_checkbox_t *cb = (fr_checkbox_t *)widget;
    if (cb->checked) {
        gfx_draw_line(&gfx_ctx, x + 3, y + 10, x + 6, y + 14, 0x0078D4);
        gfx_draw_line(&gfx_ctx, x + 6, y + 14, x + 13, y + 5, 0x0078D4);
    }

    /* 绘制文字 */
    if (widget->text[0]) {
        (void)x;
    }
}

/* ---- 滑块渲染 ---- */
static void slider_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_slider_t *slider = (fr_slider_t *)widget;

    /* 绘制轨道 */
    int track_y = y + h / 2 - 2;
    gfx_rect_t track = {x, track_y, w, 4};
    gfx_fill_rect(&gfx_ctx, track, 0xC0C0C0);

    /* 绘制已填充部分 */
    int range = slider->max_val - slider->min_val;
    if (range > 0) {
        int fill_w = (slider->value - slider->min_val) * w / range;
        gfx_rect_t fill = {x, track_y, fill_w, 4};
        gfx_fill_rect(&gfx_ctx, fill, 0x0078D4);
    }

    /* 绘制滑块手柄 */
    int thumb_x = x + (slider->value - slider->min_val) * w / (range > 0 ? range : 1);
    gfx_fill_circle(&gfx_ctx, thumb_x, y + h / 2, 8, 0x0078D4);
}

/* ---- 进度条渲染 ---- */
static void progress_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_progress_t *prog = (fr_progress_t *)widget;

    /* 背景 */
    gfx_rect_t bg_rect = {x, y, w, h};
    gfx_fill_rounded_rect(&gfx_ctx, bg_rect, 4, 0xE0E0E0);

    /* 填充 */
    if (prog->max_val > 0) {
        int fill_w = prog->value * w / prog->max_val;
        if (fill_w > 0) {
            gfx_rect_t fill_rect = {x, y, fill_w, h};
            uint32_t fill = (prog->fill_color.r << 16) |
                           (prog->fill_color.g << 8) | prog->fill_color.b;
            gfx_fill_rounded_rect(&gfx_ctx, fill_rect, 4,
                                  fill ? fill : 0x0078D4);
        }
    }
}

/* ---- 下拉框渲染 ---- */
static void combobox_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, rect, 0x808080);

    /* 下拉箭头 */
    gfx_draw_line(&gfx_ctx, x + w - 20, y + h/2 - 3, x + w - 14, y + h/2 + 3, 0x000000);
    gfx_draw_line(&gfx_ctx, x + w - 14, y + h/2 + 3, x + w - 8, y + h/2 - 3, 0x000000);

    /* 当前选中项文字 */
    fr_combobox_t *cb = (fr_combobox_t *)widget;
    if (cb->selected_index >= 0 && cb->selected_index < cb->item_count) {
        (void)cb->items[cb->selected_index];
    }
}

/* ---- 列表框渲染 ---- */
static void listbox_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, rect, 0x808080);

    /* 绘制列表项 */
    fr_listbox_t *lb = (fr_listbox_t *)widget;
    for (int i = 0; i < lb->item_count && i * 24 < h; i++) {
        int item_y = y + i * 24;
        if (i == lb->selected_index) {
            gfx_rect_t sel = {x + 1, item_y, w - 2, 24};
            gfx_fill_rect(&gfx_ctx, sel, 0xCCE8FF);
        }
        /* 绘制项文字 */
        if (lb->items && lb->items[i]) {
            (void)lb->items[i];
        }
    }
}

/* ---- 表格渲染 ---- */
static void table_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_table_t *tbl = (fr_table_t *)widget;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, rect, 0x808080);

    /* 表头 */
    gfx_rect_t header = {x, y, w, tbl->header_height > 0 ? tbl->header_height : 28};
    gfx_fill_rect(&gfx_ctx, header, 0xF0F0F0);

    /* 网格线 */
    int col_x = x;
    for (int c = 0; c <= tbl->cols; c++) {
        int cw = (tbl->col_widths && c < tbl->cols) ? tbl->col_widths[c] : w / tbl->cols;
        gfx_draw_line(&gfx_ctx, col_x, y, col_x, y + h, 0xC0C0C0);
        col_x += cw;
    }
    for (int r = 0; r <= tbl->rows; r++) {
        int row_y = y + (tbl->header_height > 0 ? tbl->header_height : 28) + r * 24;
        gfx_draw_line(&gfx_ctx, x, row_y, x + w, row_y, 0xC0C0C0);
    }
}

/* ---- 标签页渲染 ---- */
static void tabview_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_tabview_t *tv = (fr_tabview_t *)widget;

    /* 标签栏 */
    int tab_w = w / (tv->tab_count > 0 ? tv->tab_count : 1);
    for (int i = 0; i < tv->tab_count; i++) {
        int tab_x = x + i * tab_w;
        uint32_t color = (i == tv->active_tab) ? 0xFFFFFF : 0xE0E0E0;
        gfx_rect_t tab_rect = {tab_x, y, tab_w - 2, 28};
        gfx_fill_rect(&gfx_ctx, tab_rect, color);
        gfx_draw_rect(&gfx_ctx, tab_rect, 0x808080);

        /* 标签文字 */
        if (tv->tab_labels && tv->tab_labels[i]) {
            (void)tv->tab_labels[i];
        }
    }

    /* 内容区域 */
    gfx_rect_t content = {x, y + 28, w, h - 28};
    gfx_fill_rect(&gfx_ctx, content, 0xFFFFFF);
}

/* ---- 菜单渲染 ---- */
static void menu_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;
    fr_menu_t *menu = (fr_menu_t *)widget;
    if (!menu->visible) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, rect, 0x808080);

    /* 菜单项 */
    for (int i = 0; i < menu->item_count; i++) {
        int item_y = y + i * 28;
        if (menu->item_types && menu->item_types[i] == 1) {
            /* 分隔线 */
            gfx_draw_line(&gfx_ctx, x + 4, item_y + 14, x + w - 4, item_y + 14, 0xC0C0C0);
        } else {
            if (i == menu->selected_index) {
                gfx_rect_t sel = {x + 1, item_y, w - 2, 28};
                gfx_fill_rect(&gfx_ctx, sel, 0xCCE8FF);
            }
            if (menu->item_labels && menu->item_labels[i]) {
                (void)menu->item_labels[i];
            }
        }
    }
}

/* ---- 工具栏渲染 ---- */
static void toolbar_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xF0F0F0);
    gfx_draw_line(&gfx_ctx, x, y + h - 1, x + w, y + h - 1, 0xC0C0C0);
}

/* ---- 状态栏渲染 ---- */
static void statusbar_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xF0F0F0);
    gfx_draw_line(&gfx_ctx, x, y, x + w, y, 0xC0C0C0);
}

/* ---- 对话框渲染 ---- */
static void dialog_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    /* 阴影 */
    gfx_rect_t shadow = {x + 4, y + 4, w, h};
    gfx_fill_rect(&gfx_ctx, shadow, 0x40000000);

    /* 背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rounded_rect(&gfx_ctx, rect, 8, 0xFFFFFF);
    gfx_draw_rounded_rect(&gfx_ctx, rect, 8, 0x808080);

    /* 标题栏 */
    gfx_rect_t title = {x, y, w, 32};
    gfx_fill_rect(&gfx_ctx, title, 0xF0F0F0);
    gfx_draw_line(&gfx_ctx, x, y + 32, x + w, y + 32, 0xC0C0C0);

    /* 标题文字 */
    if (widget->text[0]) {
        (void)widget->text;
    }

    /* 关闭按钮 */
    gfx_rect_t close = {x + w - 28, y + 4, 24, 24};
    gfx_fill_rect(&gfx_ctx, close, 0xE0E0E0);
}

/* ---- 滚动条渲染 ---- */
static void scrollbar_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_scrollbar_t *sb = (fr_scrollbar_t *)widget;

    /* 轨道 */
    gfx_rect_t track = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, track, 0xF0F0F0);

    /* 滑块 */
    if (sb->orientation == 0) {
        /* 水平 */
        int thumb_w = sb->thumb_size > 0 ? sb->thumb_size : 30;
        int thumb_x = x + sb->thumb_pos;
        gfx_rect_t thumb = {thumb_x, y + 2, thumb_w, h - 4};
        gfx_fill_rounded_rect(&gfx_ctx, thumb, 4, 0xC0C0C0);
    } else {
        /* 垂直 */
        int thumb_h = sb->thumb_size > 0 ? sb->thumb_size : 30;
        int thumb_y = y + sb->thumb_pos;
        gfx_rect_t thumb = {x + 2, thumb_y, w - 4, thumb_h};
        gfx_fill_rounded_rect(&gfx_ctx, thumb, 4, 0xC0C0C0);
    }
}

/* ================================================================
 *  公共 API 实现
 * ================================================================ */

fr_handle_t fr_create_button(fr_handle_t parent, const char *text, fr_rect_t bounds)
{
    fr_button_t *btn = (fr_button_t *)widget_alloc(FR_WIDGET_BUTTON, sizeof(fr_button_t) - sizeof(fr_widget_t));
    if (btn == NULL) return NULL;

    btn->base.bounds = bounds;
    btn->base.bg_color = FR_RGB(230, 230, 230);
    btn->base.fg_color = FR_COLOR_BLACK;
    btn->base.render = button_render;
    btn->border_radius = 4;
    btn->hover_color = FR_RGB(220, 220, 220);
    btn->press_color = FR_RGB(180, 180, 180);

    if (text) {
        for (int i = 0; i < 255 && text[i]; i++)
            btn->base.text[i] = text[i];
    }

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)btn);
    return (fr_handle_t)btn;
}

fr_handle_t fr_create_label(fr_handle_t parent, const char *text, fr_rect_t bounds)
{
    fr_label_t *lbl = (fr_label_t *)widget_alloc(FR_WIDGET_LABEL, sizeof(fr_label_t) - sizeof(fr_widget_t));
    if (lbl == NULL) return NULL;

    lbl->base.bounds = bounds;
    lbl->base.bg_color = FR_COLOR_TRANSPARENT;
    lbl->base.fg_color = FR_COLOR_BLACK;
    lbl->base.render = label_render;
    lbl->wrap = 0;
    lbl->alignment = 0;

    if (text) {
        for (int i = 0; i < 255 && text[i]; i++)
            lbl->base.text[i] = text[i];
    }

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)lbl);
    return (fr_handle_t)lbl;
}

fr_handle_t fr_create_textbox(fr_handle_t parent, const char *text, fr_rect_t bounds)
{
    fr_textbox_t *tb = (fr_textbox_t *)widget_alloc(FR_WIDGET_TEXTBOX, sizeof(fr_textbox_t) - sizeof(fr_widget_t));
    if (tb == NULL) return NULL;

    tb->base.bounds = bounds;
    tb->base.bg_color = FR_COLOR_WHITE;
    tb->base.fg_color = FR_COLOR_BLACK;
    tb->base.render = textbox_render;
    tb->cursor_pos = 0;
    tb->selection_start = 0;
    tb->selection_end = 0;
    tb->max_length = 255;
    tb->password_mode = 0;
    tb->multiline = 0;
    tb->scroll_offset = 0;

    if (text) {
        for (int i = 0; i < 255 && text[i]; i++) {
            tb->base.text[i] = text[i];
            tb->cursor_pos++;
        }
    }

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tb);
    return (fr_handle_t)tb;
}

fr_handle_t fr_create_checkbox(fr_handle_t parent, const char *text, int checked, fr_rect_t bounds)
{
    fr_checkbox_t *cb = (fr_checkbox_t *)widget_alloc(FR_WIDGET_CHECKBOX, sizeof(fr_checkbox_t) - sizeof(fr_widget_t));
    if (cb == NULL) return NULL;

    cb->base.bounds = bounds;
    cb->base.bg_color = FR_COLOR_TRANSPARENT;
    cb->base.fg_color = FR_COLOR_BLACK;
    cb->base.render = checkbox_render;
    cb->checked = checked;
    if (checked) cb->base.state |= FR_STATE_CHECKED;

    if (text) {
        for (int i = 0; i < 255 && text[i]; i++)
            cb->base.text[i] = text[i];
    }

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)cb);
    return (fr_handle_t)cb;
}

fr_handle_t fr_create_slider(fr_handle_t parent, int min, int max, int value, fr_rect_t bounds)
{
    fr_slider_t *sl = (fr_slider_t *)widget_alloc(FR_WIDGET_SLIDER, sizeof(fr_slider_t) - sizeof(fr_widget_t));
    if (sl == NULL) return NULL;

    sl->base.bounds = bounds;
    sl->base.bg_color = FR_COLOR_TRANSPARENT;
    sl->base.fg_color = FR_COLOR_BLACK;
    sl->base.render = slider_render;
    sl->min_val = min;
    sl->max_val = max;
    sl->value = value;
    sl->orientation = 0;
    sl->tick_interval = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sl);
    return (fr_handle_t)sl;
}

fr_handle_t fr_create_progress(fr_handle_t parent, int value, int max, fr_rect_t bounds)
{
    fr_progress_t *pg = (fr_progress_t *)widget_alloc(FR_WIDGET_PROGRESS, sizeof(fr_progress_t) - sizeof(fr_widget_t));
    if (pg == NULL) return NULL;

    pg->base.bounds = bounds;
    pg->base.bg_color = FR_COLOR_LIGHT_GRAY;
    pg->base.fg_color = FR_COLOR_BLACK;
    pg->base.render = progress_render;
    pg->value = value;
    pg->max_val = max;
    pg->show_percent = 0;
    pg->fill_color = FR_RGB(0, 120, 212);

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)pg);
    return (fr_handle_t)pg;
}

fr_handle_t fr_create_combobox(fr_handle_t parent, fr_rect_t bounds)
{
    fr_combobox_t *cb = (fr_combobox_t *)widget_alloc(FR_WIDGET_COMBOBOX, sizeof(fr_combobox_t) - sizeof(fr_widget_t));
    if (cb == NULL) return NULL;

    cb->base.bounds = bounds;
    cb->base.bg_color = FR_COLOR_WHITE;
    cb->base.fg_color = FR_COLOR_BLACK;
    cb->base.render = combobox_render;
    cb->items = NULL;
    cb->item_count = 0;
    cb->selected_index = -1;
    cb->dropdown_open = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)cb);
    return (fr_handle_t)cb;
}

fr_handle_t fr_create_listbox(fr_handle_t parent, fr_rect_t bounds)
{
    fr_listbox_t *lb = (fr_listbox_t *)widget_alloc(FR_WIDGET_LISTBOX, sizeof(fr_listbox_t) - sizeof(fr_widget_t));
    if (lb == NULL) return NULL;

    lb->base.bounds = bounds;
    lb->base.bg_color = FR_COLOR_WHITE;
    lb->base.fg_color = FR_COLOR_BLACK;
    lb->base.render = listbox_render;
    lb->items = NULL;
    lb->item_count = 0;
    lb->selected_index = -1;
    lb->top_item = 0;
    lb->visible_items = (int)bounds.h / 24;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)lb);
    return (fr_handle_t)lb;
}

fr_handle_t fr_create_table(fr_handle_t parent, int cols, int rows, fr_rect_t bounds)
{
    fr_table_t *tbl = (fr_table_t *)widget_alloc(FR_WIDGET_TABLE, sizeof(fr_table_t) - sizeof(fr_widget_t));
    if (tbl == NULL) return NULL;

    tbl->base.bounds = bounds;
    tbl->base.bg_color = FR_COLOR_WHITE;
    tbl->base.fg_color = FR_COLOR_BLACK;
    tbl->base.render = table_render;
    tbl->cols = cols;
    tbl->rows = rows;
    tbl->cells = NULL;
    tbl->col_widths = NULL;
    tbl->selected_row = -1;
    tbl->header_height = 28;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tbl);
    return (fr_handle_t)tbl;
}

fr_handle_t fr_create_tabview(fr_handle_t parent, fr_rect_t bounds)
{
    fr_tabview_t *tv = (fr_tabview_t *)widget_alloc(FR_WIDGET_TABVIEW, sizeof(fr_tabview_t) - sizeof(fr_widget_t));
    if (tv == NULL) return NULL;

    tv->base.bounds = bounds;
    tv->base.bg_color = FR_COLOR_WHITE;
    tv->base.fg_color = FR_COLOR_BLACK;
    tv->base.render = tabview_render;
    tv->tab_labels = NULL;
    tv->tab_count = 0;
    tv->active_tab = 0;
    tv->tab_pages = NULL;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tv);
    return (fr_handle_t)tv;
}

fr_handle_t fr_create_menu(fr_handle_t parent, fr_rect_t bounds)
{
    fr_menu_t *menu = (fr_menu_t *)widget_alloc(FR_WIDGET_MENU, sizeof(fr_menu_t) - sizeof(fr_widget_t));
    if (menu == NULL) return NULL;

    menu->base.bounds = bounds;
    menu->base.bg_color = FR_COLOR_WHITE;
    menu->base.fg_color = FR_COLOR_BLACK;
    menu->base.render = menu_render;
    menu->item_labels = NULL;
    menu->item_types = NULL;
    menu->item_count = 0;
    menu->selected_index = -1;
    menu->visible = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)menu);
    return (fr_handle_t)menu;
}

fr_handle_t fr_create_toolbar(fr_handle_t parent, fr_rect_t bounds)
{
    fr_toolbar_t *tb = (fr_toolbar_t *)widget_alloc(FR_WIDGET_TOOLBAR, sizeof(fr_toolbar_t) - sizeof(fr_widget_t));
    if (tb == NULL) return NULL;

    tb->base.bounds = bounds;
    tb->base.bg_color = FR_RGB(240, 240, 240);
    tb->base.fg_color = FR_COLOR_BLACK;
    tb->base.render = toolbar_render;
    tb->buttons = NULL;
    tb->button_count = 0;
    tb->orientation = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tb);
    return (fr_handle_t)tb;
}

fr_handle_t fr_create_statusbar(fr_handle_t parent, fr_rect_t bounds)
{
    fr_statusbar_t *sb = (fr_statusbar_t *)widget_alloc(FR_WIDGET_STATUSBAR, sizeof(fr_statusbar_t) - sizeof(fr_widget_t));
    if (sb == NULL) return NULL;

    sb->base.bounds = bounds;
    sb->base.bg_color = FR_RGB(240, 240, 240);
    sb->base.fg_color = FR_COLOR_BLACK;
    sb->base.render = statusbar_render;
    sb->section_count = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sb);
    return (fr_handle_t)sb;
}

fr_handle_t fr_create_dialog(fr_handle_t parent, const char *title, fr_rect_t bounds)
{
    fr_dialog_t *dlg = (fr_dialog_t *)widget_alloc(FR_WIDGET_DIALOG, sizeof(fr_dialog_t) - sizeof(fr_widget_t));
    if (dlg == NULL) return NULL;

    dlg->base.bounds = bounds;
    dlg->base.bg_color = FR_COLOR_WHITE;
    dlg->base.fg_color = FR_COLOR_BLACK;
    dlg->base.render = dialog_render;
    dlg->dialog_type = 0;
    dlg->result = 0;
    dlg->content = NULL;

    if (title) {
        for (int i = 0; i < 255 && title[i]; i++)
            dlg->base.text[i] = title[i];
    }

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)dlg);
    return (fr_handle_t)dlg;
}

fr_handle_t fr_create_scrollbar(fr_handle_t parent, int orient, fr_rect_t bounds)
{
    fr_scrollbar_t *sb = (fr_scrollbar_t *)widget_alloc(FR_WIDGET_SCROLLBAR, sizeof(fr_scrollbar_t) - sizeof(fr_widget_t));
    if (sb == NULL) return NULL;

    sb->base.bounds = bounds;
    sb->base.bg_color = FR_RGB(240, 240, 240);
    sb->base.fg_color = FR_COLOR_BLACK;
    sb->base.render = scrollbar_render;
    sb->orientation = orient;
    sb->min_val = 0;
    sb->max_val = 100;
    sb->value = 0;
    sb->page_size = 10;
    sb->thumb_size = 30;
    sb->thumb_pos = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sb);
    return (fr_handle_t)sb;
}

/* ---- 数值微调框 (Spinbox) 渲染 ---- */
static void spinbox_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_spinbox_t *sb = (fr_spinbox_t *)widget;

    /* 背景 */
    gfx_rect_t bg = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, bg, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, bg, 0x808080);

    /* 减号按钮区域 */
    int btn_w = 20;
    gfx_rect_t minus_btn = {x + 2, y + 2, btn_w, h - 4};
    gfx_fill_rounded_rect(&gfx_ctx, minus_btn, 2,
        (widget->state & FR_STATE_PRESSED) ? 0xC0C0C0 : 0xE8E8E8);
    /* 减号符号 */
    gfx_draw_line(&gfx_ctx, x + 7, y + h/2, x + btn_w - 3, y + h/2, 0x000000);

    /* 数值显示区域 */
    char val_str[16];
    /* 简单整数转字符串 */
    int v = sb->value, pos = 15;
    val_str[pos--] = '\0';
    if (v == 0) val_str[pos--] = '0';
    else { while (v > 0 && pos >= 0) { val_str[pos--] = '0' + (v % 10); v /= 10; } }
    const char *vs = &val_str[pos + 1];
    (void)vs; /* 值文字由字体引擎绘制 */

    /* 加号按钮区域 */
    gfx_rect_t plus_btn = {x + w - btn_w - 2, y + 2, btn_w, h - 4};
    gfx_fill_rounded_rect(&gfx_ctx, plus_btn, 2, 0xE8E8E8);
    /* 加号符号 */
    gfx_draw_line(&gfx_ctx, x + w - btn_w + 5, y + h/2,
                   x + w - 6, y + h/2, 0x000000); /* 横线 */
    gfx_draw_line(&gfx_ctx, x + w - btn_w/2 - 2, y + 4,
                   x + w - btn_w/2 - 2, y + h - 4, 0x000000); /* 竖线 */
}

static void spinbox_handle_event(fr_widget_t *widget, void *event)
{
    fr_spinbox_t *sb = (fr_spinbox_t *)widget;
    fr_mouse_event_t *mevt = (fr_mouse_event_t *)event;

    if (mevt && mevt->event_type == FR_MOUSE_EVENT_PRESS &&
        mevt->button_changed & FR_MOUSE_LEFT) {
        int w = (int)widget->bounds.w;
        int btn_w = 20;
        int mx = mevt->x - (int)widget->bounds.x;

        if (mx < btn_w + 4) {
            /* 点击减号 */
            sb->value -= sb->step;
            if (sb->value < sb->min_val)
                sb->value = sb->wrap ? sb->max_val : sb->min_val;
        } else if (mx > w - btn_w - 4) {
            /* 点击加号 */
            sb->value += sb->step;
            if (sb->value > sb->max_val)
                sb->value = sb->wrap ? sb->min_val : sb->max_val;
        }

        if (widget->on_change)
            widget->on_change(widget, widget->user_data);
    }

    (void)sb;
}

fr_handle_t fr_create_spinbox(fr_handle_t parent, int min, int max,
                               int value, int step, fr_rect_t bounds)
{
    fr_spinbox_t *sb = (fr_spinbox_t *)widget_alloc(
        FR_WIDGET_SPINBOX, sizeof(fr_spinbox_t) - sizeof(fr_widget_t));
    if (!sb) return NULL;

    sb->base.bounds = bounds;
    sb->base.bg_color = FR_COLOR_WHITE;
    sb->base.fg_color = FR_COLOR_BLACK;
    sb->base.render = spinbox_render;
    sb->base.handle_event = spinbox_handle_event;
    sb->value = value;
    sb->min_val = min;
    sb->max_val = max;
    sb->step = step > 0 ? step : 1;
    sb->wrap = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sb);
    return (fr_handle_t)sb;
}

/* ---- 增强 H/V 滑块渲染 ---- */
static void hvslider_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_hslider_t *sl = (fr_hslider_t *)widget;

    if (sl->orientation == 0) {
        /* 水平滑块: 轨道在中间, 手柄在轨道上 */
        int track_y = y + h / 2 - 3;
        gfx_rect_t track = {x, track_y, w, 6};
        gfx_fill_rounded_rect(&gfx_ctx, track, 3, 0xD0D0D0);

        int range = sl->max_val - sl->min_val;
        if (range > 0) {
            int thumb_x = x + (sl->value - sl->min_val) * (w - 20) / range;
            gfx_rect_t thumb = {thumb_x, y, 20, h};
            uint32_t fill_color = (widget->state & FR_STATE_PRESSED) ?
                                  0x0078D4 : 0x5090D4;
            gfx_fill_rounded_rect(&gfx_ctx, thumb, 4, fill_color);
        }
    } else {
        /* 垂直滑块 */
        int track_x = x + w / 2 - 3;
        gfx_rect_t track = {track_x, y, 6, h};
        gfx_fill_rounded_rect(&gfx_ctx, track, 3, 0xD0D0D0);

        int range = sl->max_val - sl->min_val;
        if (range > 0) {
            int thumb_y = y + h - 20 -
                (sl->value - sl->min_val) * (h - 20) / range;
            gfx_rect_t thumb = {x, thumb_y, w, 20};
            gfx_fill_rounded_rect(&gfx_ctx, thumb, 4, 0x5090D4);
        }
    }
}

static void hvslider_handle_event(fr_widget_t *widget, void *event)
{
    fr_hslider_t *sl = (fr_hslider_t *)widget;
    fr_mouse_event_t *mevt = (fr_mouse_event_t *)event;

    if (mevt && (mevt->event_type == FR_MOUSE_EVENT_PRESS ||
                 mevt->event_type == FR_MOUSE_EVENT_MOVE)) {
        if (mevt->buttons & FR_MOUSE_LEFT) {
            int wx = mevt->x - (int)widget->bounds.x;
            int wy = mevt->y - (int)widget->bounds.y;
            int ww = (int)widget->bounds.w;
            int wh = (int)widget->bounds.h;
            int range = sl->max_val - sl->min_val;

            if (range > 0) {
                if (sl->orientation == 0) {
                    int new_val = sl->min_val +
                        wx * range / (ww > 20 ? ww - 20 : ww);
                    if (new_val < sl->min_val) new_val = sl->min_val;
                    if (new_val > sl->max_val) new_val = sl->max_val;
                    sl->value = new_val;
                } else {
                    int new_val = sl->min_val +
                        (wh - wy) * range / (wh > 20 ? wh - 20 : wh);
                    if (new_val < sl->min_val) new_val = sl->min_val;
                    if (new_val > sl->max_val) new_val = sl->max_val;
                    sl->value = new_val;
                }

                if (widget->on_change)
                    widget->on_change(widget, widget->user_data);
            }
        }
    }

    (void)sl;
}

fr_handle_t fr_create_hvslider(fr_handle_t parent, int orient, int min,
                                int max, int value, fr_rect_t bounds)
{
    fr_hslider_t *sl = (fr_hslider_t *)widget_alloc(
        (orient == 0) ? FR_WIDGET_HSLIDER : FR_WIDGET_VSLIDER,
        sizeof(fr_hslider_t) - sizeof(fr_widget_t));
    if (!sl) return NULL;

    sl->base.bounds = bounds;
    sl->base.bg_color = FR_COLOR_TRANSPARENT;
    sl->base.fg_color = FR_COLOR_BLACK;
    sl->base.render = hvslider_render;
    sl->base.handle_event = hvslider_handle_event;
    sl->min_val = min;
    sl->max_val = max;
    sl->value = value;
    sl->orientation = orient;
    sl->tick_interval = 0;
    sl->inverted = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sl);
    return (fr_handle_t)sl;
}

/* ---- 增强进度条渲染 ---- */
static void progress2_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_progress2_t *prog = (fr_progress2_t *)widget;

    /* 背景圆角矩形 */
    gfx_rect_t bg = {x, y, w, h};
    uint32_t bg_col = prog->bg_color.r | (prog->bg_color.g << 8) | (prog->bg_color.b << 16);
    gfx_fill_rounded_rect(&gfx_ctx, bg, h / 2, bg_col ? bg_col : 0xE8E8E8);

    /* 填充部分 */
    if (prog->max_val > 0 && prog->value > 0) {
        int fill_w = prog->value * w / prog->max_val;
        if (fill_w > 0) {
            gfx_rect_t fill = {x, y, fill_w, h};
            uint32_t fc = prog->fill_color.r |
                          (prog->fill_color.g << 8) |
                          (prog->fill_color.b << 16);
            gfx_fill_rounded_rect(&gfx_ctx, fill, h / 2,
                fc ? fc : 0x0078D4);
        }
    }

    /* 显示百分比文字 */
    if (prog->show_percent && prog->max_val > 0) {
        int pct = prog->value * 100 / prog->max_val;
        (void)pct; /* 由字体引擎绘制 */
    }
}

fr_handle_t fr_create_progress2(fr_handle_t parent, int value, int max,
                                 int show_pct, fr_rect_t bounds)
{
    fr_progress2_t *pg = (fr_progress2_t *)widget_alloc(
        FR_WIDGET_PROGRESS2, sizeof(fr_progress2_t) - sizeof(fr_widget_t));
    if (!pg) return NULL;

    pg->base.bounds = bounds;
    pg->base.bg_color = FR_COLOR_LIGHT_GRAY;
    pg->base.fg_color = FR_COLOR_BLACK;
    pg->base.render = progress2_render;
    pg->value = value;
    pg->max_val = max;
    pg->show_percent = show_pct;
    pg->fill_color = FR_RGB(0, 120, 212);
    pg->text_color = FR_COLOR_WHITE;
    pg->animated = 0;
    pg->display_value = (float)value;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)pg);
    return (fr_handle_t)pg;
}

/* ---- 标签容器 (Tab Control) 渲染 ---- */
static void tab_control_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_tab_control_t *tc = (fr_tab_control_t *)widget;

    int header_h = 30; /* 标签栏高度 */

    /* 绘制标签栏背景 */
    gfx_rect_t tab_bar = {x, y, w, header_h};
    gfx_fill_rect(&gfx_ctx, tab_bar, 0xF0F0F0);
    gfx_draw_line(&gfx_ctx, x, y + header_h - 1, x + w, y + header_h - 1, 0xC0C0C0);

    /* 绘制各个标签 */
    if (tc->tab_count > 0) {
        int tab_w = w / (int)tc->tab_count;
        for (uint32_t i = 0; i < tc->tab_count; i++) {
            int tx = x + i * tab_w;
            /* 活动标签高亮 */
            uint32_t bg = (i == tc->active_tab) ? 0xFFFFFF : 0xE0E0E0;
            gfx_rect_t tr = {tx, y, tab_w - 2, header_h - 2};
            gfx_fill_rect(&gfx_ctx, tr, bg);
            gfx_draw_rect(&gfx_ctx, tr, 0x808080);

            /* 标签文字 */
            if (tc->tab_labels && tc->tab_labels[i]) {
                (void)tc->tab_labels[i];
            }
        }
    }

    /* 内容区域 */
    gfx_rect_t content = {x, y + header_h, w, h - header_h};
    gfx_fill_rect(&gfx_ctx, content, 0xFFFFFF);
}

fr_handle_t fr_create_tab_control(fr_handle_t parent, fr_rect_t bounds)
{
    fr_tab_control_t *tc = (fr_tab_control_t *)widget_alloc(
        FR_WIDGET_TAB_CONTROL, sizeof(fr_tab_control_t) - sizeof(fr_widget_t));
    if (!tc) return NULL;

    tc->base.bounds = bounds;
    tc->base.bg_color = FR_COLOR_WHITE;
    tc->base.fg_color = FR_COLOR_BLACK;
    tc->base.render = tab_control_render;
    tc->tab_labels = NULL;
    tc->tab_count = 0;
    tc->active_tab = 0;
    tc->tab_pages = NULL;
    tc->closable = 0;
    tc->tab_position = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tc);
    return (fr_handle_t)tc;
}

/* ---- 层级树形视图 (Tree View) 渲染 ---- */
/* 递归绘制树节点 */
static void tree_view_draw_node(fr_tree_node_t *node, int *idx, int x, int y,
                                  gfx_context_t *gfx, int indent_px, int selected_idx,
                                  fr_tree_view_t *tv)
{
    if (node == NULL || (*idx) >= 25) return;

    int node_y = y + (*idx) * 24;
    int node_x = x + node->level * indent_px;

    /* 选中高亮 */
    if ((*idx) == selected_idx) {
        gfx_rect_t sel = {node_x, node_y, 300, 24};
        gfx_fill_rect(gfx, sel, 0xCCE8FF);
    }

    /* 展开/折叠指示器 (+/-) */
    if (node->first_child) {
        int cx = node_x + 4;
        int cy = node_y + 12;
        gfx_rect_t box = {cx - 4, cy - 4, 8, 8};
        gfx_fill_rect(gfx, box, 0xFFFFFF);
        gfx_draw_rect(gfx, box, 0x808080);
        /* + 或 - 符号 */
        gfx_draw_line(gfx, cx - 2, cy, cx + 2, cy, 0x000000);
        if (!node->expanded)
            gfx_draw_line(gfx, cx, cy - 2, cx, cy + 2, 0x000000);
    }

    /* 连接线 */
    if (tv->show_lines && node->level > 0) {
        for (int l = 0; l < node->level; l++)
            gfx_draw_line(gfx, node_x + l * indent_px + 8, node_y + 12,
                           node_x + l * indent_px + 8, node_y + 24, 0xD0D0D0);
    }

    /* 节点文字 */
    (void)node->text;

    (*idx)++;
}

static void tree_view_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_tree_view_t *tv = (fr_tree_view_t *)widget;

    /* 背景 */
    gfx_rect_t bg = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, bg, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, bg, 0xC0C0C0);

    /* 递归绘制可见节点 */
    if (tv->root) {
        int idx = 0;
        tree_view_draw_node(tv->root, &idx, x, y, &gfx_ctx,
                            tv->indent_px > 0 ? tv->indent_px : 16,
                            tv->selected_node_idx, tv);
    }
}

fr_handle_t fr_create_tree_view(fr_handle_t parent, fr_rect_t bounds)
{
    fr_tree_view_t *tv = (fr_tree_view_t *)widget_alloc(
        FR_WIDGET_TREE_VIEW, sizeof(fr_tree_view_t) - sizeof(fr_widget_t));
    if (!tv) return NULL;

    tv->base.bounds = bounds;
    tv->base.bg_color = FR_COLOR_WHITE;
    tv->base.fg_color = FR_COLOR_BLACK;
    tv->base.render = tree_view_render;
    tv->root = NULL;
    tv->selected_node_idx = -1;
    tv->visible_depth = 99;
    tv->show_lines = 1;
    tv->show_root_handles = 1;
    tv->indent_px = 16;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tv);
    return (fr_handle_t)tv;
}

/* ---- 分割器 (Splitter) 渲染 ---- */
static void splitter_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_splitter_t *sp = (fr_splitter_t *)widget;

    /* 绘制分割条 */
    if (sp->orientation == 0) {
        /* 垂直分割条 (左右分) */
        int sx = sp->position > 0 ? sp->position : w / 2;
        gfx_rect_t grip = {sx - 3, y, 6, h};
        uint32_t col = sp->dragging ? 0x0078D4 : 0xD0D0D0;
        gfx_fill_rect(&gfx_ctx, grip, col);
    } else {
        /* 水平分割条 (上下分) */
        int sy = sp->position > 0 ? sp->position : h / 2;
        gfx_rect_t grip = {x, sy - 3, w, 6};
        uint32_t col = sp->dragging ? 0x0078D4 : 0xD0D0D0;
        gfx_fill_rect(&gfx_ctx, grip, col);
    }
}

static void splitter_handle_event(fr_widget_t *widget, void *event)
{
    fr_splitter_t *sp = (fr_splitter_t *)widget;
    fr_mouse_event_t *mevt = (fr_mouse_event_t *)event;

    if (mevt) {
        if (mevt->event_type == FR_MOUSE_EVENT_PRESS &&
            mevt->button_changed & FR_MOUSE_LEFT) {
            sp->dragging = 1;
            sp->initial_pos = (sp->orientation == 0) ? mevt->x : mevt->y;
        } else if (mevt->event_type == FR_MOUSE_EVENT_RELEASE) {
            sp->dragging = 0;
        } else if (mevt->event_type == FR_MOUSE_EVENT_MOVE &&
                   sp->dragging && (mevt->buttons & FR_MOUSE_LEFT)) {
            if (sp->orientation == 0) {
                sp->position = mevt->x - (int)widget->bounds.x;
            } else {
                sp->position = mevt->y - (int)widget->bounds.y;
            }
            /* 限制在合理范围内 */
            if (sp->position < sp->min_pane_size)
                sp->position = sp->min_pane_size;
            int limit = (sp->orientation == 0) ?
                        (int)widget->bounds.w - sp->min_pane_size :
                        (int)widget->bounds.h - sp->min_pane_size;
            if (sp->position > limit) sp->position = limit;

            if (widget->on_change)
                widget->on_change(widget, widget->user_data);
        }
    }

    (void)sp;
}

fr_handle_t fr_create_splitter(fr_handle_t parent, int orient, int position,
                                fr_rect_t bounds)
{
    fr_splitter_t *sp = (fr_splitter_t *)widget_alloc(
        FR_WIDGET_SPLITTER, sizeof(fr_splitter_t) - sizeof(fr_widget_t));
    if (!sp) return NULL;

    sp->base.bounds = bounds;
    sp->base.bg_color = FR_COLOR_TRANSPARENT;
    sp->base.render = splitter_render;
    sp->base.handle_event = splitter_handle_event;
    sp->orientation = orient;
    sp->position = position;
    sp->min_pane_size = 40;
    sp->dragging = 0;
    sp->initial_pos = 0;
    sp->pane_first = NULL;
    sp->pane_second = NULL;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sp);
    return (fr_handle_t)sp;
}

/* ---- 增强工具栏 (Toolbar2) 渲染 ---- */
static void toolbar2_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_toolbar2_t *tb = (fr_toolbar2_t *)widget;

    /* 工具栏背景渐变效果 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xF5F5F5);
    /* 底部边框线 */
    gfx_draw_line(&gfx_ctx, x, y + h - 1, x + w, y + h - 1, 0xC0C0C0);

    /* 绘制工具按钮 */
    if (tb->button_count > 0) {
        int icon_sz = tb->icon_size > 0 ? tb->icon_size : 24;
        int spacing = 4;
        int btn_total = (int)(icon_sz + spacing);
        int start_x = x + 4;

        for (uint32_t i = 0; i < tb->button_count; i++) {
            int bx = start_x + i * btn_total;
            int by = y + (h - icon_sz) / 2;

            /* 按钮背景 */
            gfx_rect_t btn_bg = {bx, by, icon_sz, icon_sz};
            gfx_fill_rounded_rect(&gfx_ctx, btn_bg, 4, 0xF0F0F0);

            /* 分隔线 */
            for (uint32_t s = 0; s < tb->separator_count; s++) {
                if ((int)i == tb->separator_indices[s]) {
                    gfx_draw_line(&gfx_ctx, bx - 2, y + 4,
                                   bx - 2, y + h - 4, 0xC0C0C0);
                    break;
                }
            }

            /* 按钮标签文字 */
            if (tb->show_text && tb->button_labels && tb->button_labels[i]) {
                (void)tb->button_labels[i];
            }
        }
    }
}

fr_handle_t fr_create_toolbar2(fr_handle_t parent, fr_rect_t bounds)
{
    fr_toolbar2_t *tb = (fr_toolbar2_t *)widget_alloc(
        FR_WIDGET_TOOLBAR2, sizeof(fr_toolbar2_t) - sizeof(fr_widget_t));
    if (!tb) return NULL;

    tb->base.bounds = bounds;
    tb->base.bg_color = FR_RGB(245, 245, 245);
    tb->base.fg_color = FR_COLOR_BLACK;
    tb->base.render = toolbar2_render;
    tb->buttons = NULL;
    tb->button_labels = NULL;
    tb->button_count = 0;
    tb->icon_size = 24;
    tb->show_text = 0;
    tb->orientation = 0;
    memset(tb->separator_indices, 0, sizeof(tb->separator_indices));
    tb->separator_count = 0;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)tb);
    return (fr_handle_t)tb;
}

/* ---- 增强状态栏 (Statusbar2) 渲染 ---- */
static void statusbar2_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_statusbar2_t *sb = (fr_statusbar2_t *)widget;

    /* 状态栏背景 */
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, 0xF0F0F0);
    /* 顶部边框线 */
    gfx_draw_line(&gfx_ctx, x, y, x + w, y, 0xC0C0C0);

    /* 绘制各段内容 */
    if (sb->section_count > 0) {
        int cur_x = x + 4;
        for (uint32_t i = 0; i < sb->section_count; i++) {
            int sw = (i < sb->section_count - 1) ?
                     (w * sb->section_widths[i] / 100) :
                     (w - (cur_x - x) - 4);
            if (sw <= 0) sw = 40;

            /* 各段可使用不同颜色 */
            uint32_t sc = 0x404040;
            if (i < 8) sc = sb->section_colors[i].r |
                              (sb->section_colors[i].g << 8) |
                              (sb->section_colors[i].b << 16);
            (void)sc; /* 颜色由字体引擎使用 */

            /* 段分隔线 */
            if (i < sb->section_count - 1) {
                gfx_draw_line(&gfx_ctx, cur_x + sw, y + 2,
                               cur_x + sw, y + h - 2, 0xD0D0D0);
            }
            cur_x += sw + 4;
        }
    }

    /* 右下角拖动手柄 (size grip) */
    if (sb->show_grip) {
        int gx = x + w - 14;
        int gy = y + h - 12;
        for (int i = 0; i < 5; i++) {
            gfx_draw_line(&gfx_ctx, gx + i*3, gy + 11,
                           gx + 11, gy + i*3, 0xA0A0A0);
        }
    }
}

fr_handle_t fr_create_statusbar2(fr_handle_t parent, fr_rect_t bounds)
{
    fr_statusbar2_t *sb = (fr_statusbar2_t *)widget_alloc(
        FR_WIDGET_STATUSBAR2, sizeof(fr_statusbar2_t) - sizeof(fr_widget_t));
    if (!sb) return NULL;

    sb->base.bounds = bounds;
    sb->base.bg_color = FR_RGB(240, 240, 240);
    sb->base.fg_color = FR_COLOR_BLACK;
    sb->base.render = statusbar2_render;
    memset(sb->sections, 0, sizeof(sb->sections));
    for (int i = 0; i < 8; i++) sb->section_widths[i] = 100 / 8;
    sb->section_count = 0;
    for (int i = 0; i < 8; i++) sb->section_colors[i] = FR_RGB(64, 64, 64);
    sb->show_grip = 1;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)sb);
    return (fr_handle_t)sb;
}

/* ---- 多文档界面区域 (MDI Area) 渲染 ---- */
static void mdi_area_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_mdi_area_t *mdi = (fr_mdi_area_t *)widget;

    /* MDI 区域背景 (棋盘格或纯色) */
    gfx_rect_t bg = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, bg, 0xE8E8E8);

    /* 绘制子窗口 (级联或平铺排列) */
    if (mdi->child_count > 0 && mdi->children) {
        for (uint32_t i = 0; i < mdi->child_count; i++) {
            fr_widget_t *child = (fr_widget_t *)mdi->children[i];
            if (child == NULL) continue;

            int cw, ch, cx, cy;
            switch (mdi->arrangement) {
            case 0: /* 级联 */
                cx = x + (int)(i * mdi->cascade_offset);
                cy = y + (int)(i * mdi->cascade_offset);
                cw = (w * 2) / 3;
                ch = (h * 2) / 3;
                break;
            case 1: /* 平铺 */
                cw = w / (int)mdi->child_count;
                ch = h;
                cx = x + (int)i * cw;
                cy = y;
                break;
            case 2: /* 水平平铺 */
                cw = w;
                ch = h / (int)mdi->child_count;
                cx = x;
                cy = y + (int)i * ch;
                break;
            case 3: /* 垂直平铺 */
                cw = w / (int)mdi->child_count;
                ch = h;
                cx = x + (int)i * cw;
                cy = y;
                break;
            default:
                cw = child->bounds.w; ch = child->bounds.h;
                cx = child->bounds.x; cy = child->bounds.y;
                break;
            }

            /* 子窗口边框和阴影 */
            gfx_rect_t shadow = {cx + 3, cy + 3, cw, ch};
            gfx_fill_rect(&gfx_ctx, shadow, 0xB0000000);

            gfx_rect_t win = {cx, cy, cw, ch};
            uint32_t win_color = ((int)i == mdi->active_child) ?
                                 0xFFFFFF : 0xF8F8F8;
            gfx_fill_rect(&gfx_ctx, win, win_color);
            gfx_draw_rect(&gfx_ctx, win,
                         ((int)i == mdi->active_child) ? 0x0078D4 : 0xA0A0A0);

            /* 子窗口标题栏 */
            gfx_rect_t title = {cx, cy, cw, 22};
            gfx_fill_rect(&gfx_ctx, title, 0xE0E0E0);
            gfx_draw_line(&gfx_ctx, cx, cy + 22, cx + cw, cy + 22, 0xC0C0C0);
        }
    }
}

fr_handle_t fr_create_mdi_area(fr_handle_t parent, fr_rect_t bounds)
{
    fr_mdi_area_t *mdi = (fr_mdi_area_t *)widget_alloc(
        FR_WIDGET_MDI_AREA, sizeof(fr_mdi_area_t) - sizeof(fr_widget_t));
    if (!mdi) return NULL;

    mdi->base.bounds = bounds;
    mdi->base.bg_color = FR_RGB(232, 232, 232);
    mdi->base.fg_color = FR_COLOR_BLACK;
    mdi->base.render = mdi_area_render;
    mdi->children = NULL;
    mdi->child_count = 0;
    mdi->max_children = 16;
    mdi->active_child = 0;
    mdi->cascade_offset = 28;
    mdi->arrangement = 0; /* 默认级联 */

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)mdi);
    return (fr_handle_t)mdi;
}

/* ---- 日历控件 (Calendar) 渲染 ---- */
/* 判断是否为闰年 */
static int calendar_is_leap_year(int year) {
    return (year % 400 == 0) || (year % 100 != 0 && year % 4 == 0);
}

/* 获取月份天数 */
static int calendar_days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && calendar_is_leap_year(year)) return 29;
    return days[month - 1];
}

/* 获取某月第一天是星期几 (0=周日) */
static int calendar_first_day_of_month(int year, int month) {
    /* Zeller 公式的简化版本 */
    if (month < 3) { month += 12; year--; }
    int century = year / 100;
    int yr = year % 100;
    int day = (1 + (13 * (month + 1)) / 5 + yr + yr / 4 + century / 4 + 5 * century) % 7;
    return day; /* 0=周六 -> 转换为 0=周日 */
}

static void calendar_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_calendar_t *cal = (fr_calendar_t *)widget;

    /* 整体背景 */
    gfx_rect_t bg = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, bg, 0xFFFFFF);
    gfx_draw_rect(&gfx_ctx, bg, 0xC0C0C0);

    int header_h = cal->header_visible ? 36 : 0;
    int cell_w = w / 7;
    int cell_h = (h - header_h) / 7; /* 1行星期头 + 6行日期 */

    /* 年月导航头部 */
    if (cal->header_visible) {
        gfx_rect_t hdr = {x, y, w, header_h};
        gfx_fill_rect(&gfx_ctx, hdr, 0xF0F0F0);
        /* 左箭头 (< 上月) */
        gfx_draw_line(&gfx_ctx, x+8, y+header_h/2-4, x+8, y+header_h/2+4, 0x000000);
        gfx_draw_line(&gfx_ctx, x+5, y+header_h/2-1, x+11, y+header_h/2-1, 0x000000);
        /* 右箭头 (> 下月) */
        gfx_draw_line(&gfx_ctx, x+w-8, y+header_h/2-4, x+w-8, y+header_h/2+4, 0x000000);
        gfx_draw_line(&gfx_ctx, x+w-11, y+header_h/2-1, x+w-5, y+header_h/2-1, 0x000000);
        /* 年月文字居中 */
        (void)cal->year; (void)cal->month; /* 字体引擎绘制 */
    }

    /* 星期标题行 */
    const char *weekdays[] = {"日","一","二","三","四","五","六"};
    for (int wd = 0; wd < 7; wd++) {
        int wx = x + wd * cell_w;
        int wy = y + header_h;
        gfx_rect_t cell = {wx, wy, cell_w, 22};
        gfx_fill_rect(&gfx_ctx, cell, 0xE8E8E8);
        (void)weekdays[wd]; /* 字体引擎绘制 */
    }

    /* 日期格子 */
    int first_day = calendar_first_day_of_month(cal->year, cal->month);
    int dim = calendar_days_in_month(cal->year, cal->month);
    int day = 1;
    for (int row = 0; row < 6 && day <= dim; row++) {
        for (int col = 0; col < 7 && day <= dim; col++) {
            int dx = x + col * cell_w;
            int dy = y + header_h + 22 + row * (cell_h > 0 ? cell_h : 24);

            /* 高亮今天 */
            int is_today = (day == cal->today_day &&
                            cal->month == cal->today_month &&
                            cal->year == cal->today_year);
            /* 高亮选中日期 */
            int is_selected = (day == cal->day);

            gfx_rect_t cell = {dx, dy, cell_w - 1, cell_h > 0 ? cell_h - 1 : 23};
            if (is_selected) {
                gfx_fill_rounded_rect(&gfx_ctx, cell, 4, 0x0078D4);
            } else if (is_today) {
                gfx_fill_rounded_rect(&gfx_ctx, cell, 4, 0xFFE080);
            } else {
                gfx_fill_rect(&gfx_ctx, cell, 0xFAFAFA);
            }

            /* 日期数字 */
            char dstr[4]; dstr[0] = '0' + (day / 10); dstr[1] = '0' + (day % 10);
            dstr[2] = '\0'; if (dstr[0] == '0') { dstr[0] = dstr[1]; dstr[1] = '\0'; }
            (void)dstr;

            day++;
        }
    }
}

fr_handle_t fr_create_calendar(fr_handle_t parent, int year, int month,
                                int day, fr_rect_t bounds)
{
    fr_calendar_t *cal = (fr_calendar_t *)widget_alloc(
        FR_WIDGET_CALENDAR, sizeof(fr_calendar_t) - sizeof(fr_widget_t));
    if (!cal) return NULL;

    cal->base.bounds = bounds;
    cal->base.bg_color = FR_COLOR_WHITE;
    cal->base.fg_color = FR_COLOR_BLACK;
    cal->base.render = calendar_render;
    cal->year = year;
    cal->month = month;
    cal->day = day;
    /* 设置"今天"为当前选中日期 */
    cal->today_year = year;
    cal->today_month = month;
    cal->today_day = day;
    cal->sel_year = year;
    cal->sel_month = month;
    cal->sel_day = day;
    cal->header_visible = 1;
    cal->week_numbers = 0;
    cal->min_year = 1900;
    cal->max_year = 2100;

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)cal);
    return (fr_handle_t)cal;
}

/* ---- 颜色选择器 (Color Picker) 渲染 ---- */
static void color_picker_render(fr_widget_t *widget, fr_context_t *ctx)
{
    if (ctx == NULL || widget == NULL) return;

    int x = (int)widget->bounds.x;
    int y = (int)widget->bounds.y;
    int w = (int)widget->bounds.w;
    int h = (int)widget->bounds.h;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    fr_color_picker_t *cp = (fr_color_picker_t *)widget;

    /* 背景 */
    gfx_rect_t bg = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, bg, 0xF0F0F0);
    gfx_draw_rect(&gfx_ctx, bg, 0x808080);

    /* 当前颜色预览块 (右上角) */
    uint32_t preview_color = ((uint32_t)cp->r << 16) |
                             ((uint32_t)cp->g << 8) | cp->b;
    gfx_rect_t preview = {x + w - 48, y + 8, 40, 28};
    gfx_fill_rounded_rect(&gfx_ctx, preview, 4, preview_color);
    gfx_draw_rounded_rect(&gfx_ctx, preview, 4, 0x808080);

    /* 如果显示 Alpha, 用棋盘格表示透明度 */
    if (cp->show_alpha) {
        /* 棋盘格背景示意透明 */
        gfx_rect_t alpha_preview = {x + w - 48, y + 42, 40, 12};
        gfx_fill_rect(&gfx_ctx, alpha_preview, 0xFFFFFF);
        /* 对角线表示透明 */
        for (int i = 0; i < 12; i++) {
            gfx_draw_line(&gfx_ctx, x + w - 48 + i, y + 42 + i,
                           x + w - 47 + i, y + 42 + i, 0xD0D0D0);
        }
    }

    /* RGB 滑块模式: 三个颜色通道滑块 */
    if (cp->mode == 1) {
        int slider_w = w - 60;
        int slider_h = 18;
        int sy = y + (cp->show_alpha ? 68 : 52);

        /* R 滑块 */
        gfx_rect_t r_label = {x, sy, 16, slider_h};
        gfx_fill_rect(&gfx_ctx, r_label, 0xFFCCCC);
        gfx_rect_t r_track = {x + 20, sy, slider_w, slider_h};
        gfx_fill_rect(&gfx_ctx, r_track, 0xE0E0E0);
        int rx = x + 20 + (cp->r * slider_w / 255);
        gfx_fill_rect(&gfx_ctx, (gfx_rect_t){rx-3, sy, 6, slider_h}, 0xFF0000);

        /* G 滑块 */
        sy += slider_h + 6;
        gfx_rect_t g_label = {x, sy, 16, slider_h};
        gfx_fill_rect(&gfx_ctx, g_label, 0xCCFFCC);
        gfx_rect_t g_track = {x + 20, sy, slider_w, slider_h};
        gfx_fill_rect(&gfx_ctx, g_track, 0xE0E0E0);
        int gx = x + 20 + (cp->g * slider_w / 255);
        gfx_fill_rect(&gfx_ctx, (gfx_rect_t){gx-3, sy, 6, slider_h}, 0x00FF00);

        /* B 滑块 */
        sy += slider_h + 6;
        gfx_rect_t b_label = {x, sy, 16, slider_h};
        gfx_fill_rect(&gfx_ctx, b_label, 0xCCCCFF);
        gfx_rect_t b_track = {x + 20, sy, slider_w, slider_h};
        gfx_fill_rect(&gfx_ctx, b_track, 0xE0E0E0);
        int bx = x + 20 + (cp->b * slider_w / 255);
        gfx_fill_rect(&gfx_ctx, (gfx_rect_t){bx-3, sy, 6, slider_h}, 0x0000FF);
    }

    /* 最近使用颜色列表 */
    if (cp->recent_count > 0) {
        int csz = 18;
        int cy = y + h - csz - 6;
        for (int i = 0; i < (int)cp->recent_count && i < 12; i++) {
            int cx = x + 6 + i * (csz + 4);
            gfx_rect_t swatch = {cx, cy, csz, csz};
            gfx_fill_rounded_rect(&gfx_ctx, swatch, 3, cp->recent_colors[i]);
            gfx_draw_rounded_rect(&gfx_ctx, swatch, 3, 0xA0A0A0);
        }
    }
}

fr_handle_t fr_create_color_picker(fr_handle_t parent, fr_rect_t bounds)
{
    fr_color_picker_t *cp = (fr_color_picker_t *)widget_alloc(
        FR_WIDGET_COLOR_PICKER, sizeof(fr_color_picker_t) - sizeof(fr_widget_t));
    if (!cp) return NULL;

    cp->base.bounds = bounds;
    cp->base.bg_color = FR_COLOR_WHITE;
    cp->base.fg_color = FR_COLOR_BLACK;
    cp->base.render = color_picker_render;
    cp->r = 0; cp->g = 0; cp->b = 0; cp->a = 255;
    cp->mode = 1;           /* 默认 RGB 滑块模式 */
    cp->show_alpha = 0;
    cp->recent_count = 0;
    memset(cp->recent_colors, 0, sizeof(cp->recent_colors));

    widget_add_child((fr_widget_t *)parent, (fr_widget_t *)cp);
    return (fr_handle_t)cp;
}

void fr_set_text(fr_handle_t widget, const char *text)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;

    for (int i = 0; i < 255 && text[i]; i++)
        w->text[i] = text[i];
    w->text[255] = '\0';
    w->state |= FR_STATE_DIRTY;
}

const char *fr_get_text(fr_handle_t widget)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    return w ? w->text : NULL;
}

void fr_set_color(fr_handle_t widget, fr_color_t fg, fr_color_t bg)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;
    w->fg_color = fg;
    w->bg_color = bg;
    w->state |= FR_STATE_DIRTY;
}

void fr_set_visible(fr_handle_t widget, int visible)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;
    if (visible) w->state |= FR_STATE_VISIBLE;
    else w->state &= ~FR_STATE_VISIBLE;
}

void fr_set_enabled(fr_handle_t widget, int enabled)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;
    if (enabled) w->state |= FR_STATE_ENABLED;
    else w->state &= ~FR_STATE_ENABLED;
}

void fr_destroy_widget(fr_handle_t widget)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;

    /* 递归销毁子控件 */
    fr_widget_t *child = w->first_child;
    while (child) {
        fr_widget_t *next = child->next_sibling;
        fr_destroy_widget((fr_handle_t)child);
        child = next;
    }

    /* 从父控件中移除 */
    if (w->parent) {
        fr_widget_t **pp = &w->parent->first_child;
        while (*pp) {
            if (*pp == w) {
                *pp = w->next_sibling;
                break;
            }
            pp = &(*pp)->next_sibling;
        }
    }

    fr_free(w);
}
