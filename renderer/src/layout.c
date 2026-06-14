/* layout.c - 布局管理器实现
 * 水平/垂直/网格/弹性/锚点布局
 */

#include "funrender.h"
#include "fr_layout.h"
#include "fr_context.h"

/* 创建布局 */
fr_layout_t *fr_layout_create(int type)
{
    fr_layout_t *layout = (fr_layout_t *)fr_alloc(sizeof(fr_layout_t));
    if (layout == NULL) return NULL;

    layout->type = type;
    layout->spacing = 4;
    layout->margin = 4;
    layout->cols = 1;
    layout->rows = 1;
    layout->direction = 0;
    layout->wrap = 0;
    layout->justify = 0;
    layout->align_items = 0;
    layout->anchor_flags = 0;
    layout->offset_left = 0;
    layout->offset_right = 0;
    layout->offset_top = 0;
    layout->offset_bottom = 0;

    return layout;
}

/* 销毁布局 */
void fr_layout_destroy(fr_layout_t *layout)
{
    if (layout) fr_free(layout);
}

/* 应用水平布局 */
static void layout_hbox_apply(fr_layout_t *layout, fr_widget_t *container)
{
    fr_widget_t *child = container->first_child;
    int count = 0;
    int total_min_w = 0;

    /* 计算子控件数量和总最小宽度 */
    while (child) {
        if (child->state & FR_STATE_VISIBLE) {
            count++;
            total_min_w += child->min_width;
        }
        child = child->next_sibling;
    }

    if (count == 0) return;

    int avail_w = (int)container->bounds.w - layout->margin * 2 - layout->spacing * (count - 1);
    int avail_h = (int)container->bounds.h - layout->margin * 2;
    int child_w = avail_w / count;
    int x = (int)container->bounds.x + layout->margin;
    int y = (int)container->bounds.y + layout->margin;

    child = container->first_child;
    while (child) {
        if (child->state & FR_STATE_VISIBLE) {
            child->bounds.x = (float)x;
            child->bounds.y = (float)y;
            child->bounds.w = (float)child_w;
            child->bounds.h = (float)avail_h;
            x += child_w + layout->spacing;
        }
        child = child->next_sibling;
    }
}

/* 应用垂直布局 */
static void layout_vbox_apply(fr_layout_t *layout, fr_widget_t *container)
{
    fr_widget_t *child = container->first_child;
    int count = 0;

    while (child) {
        if (child->state & FR_STATE_VISIBLE) count++;
        child = child->next_sibling;
    }

    if (count == 0) return;

    int avail_w = (int)container->bounds.w - layout->margin * 2;
    int avail_h = (int)container->bounds.h - layout->margin * 2 - layout->spacing * (count - 1);
    int child_h = avail_h / count;
    int x = (int)container->bounds.x + layout->margin;
    int y = (int)container->bounds.y + layout->margin;

    child = container->first_child;
    while (child) {
        if (child->state & FR_STATE_VISIBLE) {
            child->bounds.x = (float)x;
            child->bounds.y = (float)y;
            child->bounds.w = (float)avail_w;
            child->bounds.h = (float)child_h;
            y += child_h + layout->spacing;
        }
        child = child->next_sibling;
    }
}

/* 应用网格布局 */
static void layout_grid_apply(fr_layout_t *layout, fr_widget_t *container)
{
    fr_widget_t *child = container->first_child;
    int cols = layout->cols > 0 ? layout->cols : 1;
    int rows = layout->rows > 0 ? layout->rows : 1;

    int cell_w = ((int)container->bounds.w - layout->margin * 2 - layout->spacing * (cols - 1)) / cols;
    int cell_h = ((int)container->bounds.h - layout->margin * 2 - layout->spacing * (rows - 1)) / rows;

    int row = 0, col = 0;
    while (child) {
        if (child->state & FR_STATE_VISIBLE) {
            child->bounds.x = (float)(container->bounds.x + layout->margin + col * (cell_w + layout->spacing));
            child->bounds.y = (float)(container->bounds.y + layout->margin + row * (cell_h + layout->spacing));
            child->bounds.w = (float)cell_w;
            child->bounds.h = (float)cell_h;

            col++;
            if (col >= cols) {
                col = 0;
                row++;
            }
        }
        child = child->next_sibling;
    }
}

/* 应用锚点布局 */
static void layout_anchor_apply(fr_layout_t *layout, fr_widget_t *container)
{
    fr_widget_t *child = container->first_child;
    int flags = layout->anchor_flags;
    int cw = (int)container->bounds.w;
    int ch = (int)container->bounds.h;

    while (child) {
        if (child->state & FR_STATE_VISIBLE) {
            int x = (int)child->bounds.x;
            int y = (int)child->bounds.y;
            int w = (int)child->bounds.w;
            int h = (int)child->bounds.h;

            if (flags & FR_ANCHOR_LEFT)
                x = layout->offset_left;
            if (flags & FR_ANCHOR_RIGHT)
                x = cw - w - layout->offset_right;
            if (flags & FR_ANCHOR_HCENTER)
                x = (cw - w) / 2;
            if (flags & FR_ANCHOR_TOP)
                y = layout->offset_top;
            if (flags & FR_ANCHOR_BOTTOM)
                y = ch - h - layout->offset_bottom;
            if (flags & FR_ANCHOR_VCENTER)
                y = (ch - h) / 2;

            child->bounds.x = (float)x;
            child->bounds.y = (float)y;
        }
        child = child->next_sibling;
    }
}

/* 应用布局 */
void fr_layout_apply(fr_layout_t *layout, fr_widget_t *container)
{
    if (layout == NULL || container == NULL) return;

    switch (layout->type) {
    case FR_LAYOUT_HBOX:   layout_hbox_apply(layout, container); break;
    case FR_LAYOUT_VBOX:   layout_vbox_apply(layout, container); break;
    case FR_LAYOUT_GRID:   layout_grid_apply(layout, container); break;
    case FR_LAYOUT_ANCHOR: layout_anchor_apply(layout, container); break;
    case FR_LAYOUT_FLEX:   layout_hbox_apply(layout, container); break; /* 简化 */
    }
}

void fr_layout_set_spacing(fr_layout_t *layout, int spacing) { if (layout) layout->spacing = spacing; }
void fr_layout_set_margin(fr_layout_t *layout, int margin)   { if (layout) layout->margin = margin; }
void fr_layout_set_grid(fr_layout_t *layout, int cols, int rows) { if (layout) { layout->cols = cols; layout->rows = rows; } }
void fr_layout_set_anchor(fr_layout_t *layout, int flags, int l, int t, int r, int b)
{
    if (layout) { layout->anchor_flags = flags; layout->offset_left = l; layout->offset_top = t; layout->offset_right = r; layout->offset_bottom = b; }
}
void fr_layout_set_flex(fr_layout_t *layout, int dir, int wrap, int justify, int align)
{
    if (layout) { layout->direction = dir; layout->wrap = wrap; layout->justify = justify; layout->align_items = align; }
}

/* 公共布局 API */
fr_handle_t fr_layout_hbox(fr_handle_t parent, int spacing, int margin)
{
    fr_layout_t *l = fr_layout_create(FR_LAYOUT_HBOX);
    if (l) { l->spacing = spacing; l->margin = margin; }
    if (parent) ((fr_widget_t *)parent)->layout = l;
    return (fr_handle_t)l;
}

fr_handle_t fr_layout_vbox(fr_handle_t parent, int spacing, int margin)
{
    fr_layout_t *l = fr_layout_create(FR_LAYOUT_VBOX);
    if (l) { l->spacing = spacing; l->margin = margin; }
    if (parent) ((fr_widget_t *)parent)->layout = l;
    return (fr_handle_t)l;
}

fr_handle_t fr_layout_grid(fr_handle_t parent, int cols, int rows, int spacing)
{
    fr_layout_t *l = fr_layout_create(FR_LAYOUT_GRID);
    if (l) { l->cols = cols; l->rows = rows; l->spacing = spacing; }
    if (parent) ((fr_widget_t *)parent)->layout = l;
    return (fr_handle_t)l;
}

fr_handle_t fr_layout_anchor(fr_handle_t parent, int anchor_flags)
{
    fr_layout_t *l = fr_layout_create(FR_LAYOUT_ANCHOR);
    if (l) l->anchor_flags = anchor_flags;
    if (parent) ((fr_widget_t *)parent)->layout = l;
    return (fr_handle_t)l;
}
