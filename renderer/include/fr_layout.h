/* fr_layout.h - 布局系统
 * 水平/垂直/网格/弹性/锚点布局管理器
 */

#ifndef FR_LAYOUT_H
#define FR_LAYOUT_H

#include "stdint.h"

/* 布局类型 */
#define FR_LAYOUT_HBOX     1   /* 水平布局 */
#define FR_LAYOUT_VBOX     2   /* 垂直布局 */
#define FR_LAYOUT_GRID     3   /* 网格布局 */
#define FR_LAYOUT_FLEX     4   /* 弹性布局 */
#define FR_LAYOUT_ANCHOR   5   /* 锚点布局 */

/* 对齐方式 */
#define FR_ALIGN_LEFT      0
#define FR_ALIGN_CENTER    1
#define FR_ALIGN_RIGHT     2
#define FR_ALIGN_TOP       0
#define FR_ALIGN_BOTTOM    2
#define FR_ALIGN_VCENTER   1

/* 锚点标志 */
#define FR_ANCHOR_LEFT     0x01
#define FR_ANCHOR_RIGHT    0x02
#define FR_ANCHOR_TOP      0x04
#define FR_ANCHOR_BOTTOM   0x08
#define FR_ANCHOR_HCENTER  0x10
#define FR_ANCHOR_VCENTER  0x20

/* 弹性参数 */
#define FR_FLEX_GROW       0x01
#define FR_FLEX_SHRINK     0x02
#define FR_FLEX_BASIS_AUTO 0x04

/* 布局结构 */
typedef struct fr_layout {
    uint32_t type;
    int spacing;
    int margin;

    /* 网格参数 */
    int cols;
    int rows;

    /* 弹性参数 */
    int direction;         /* 0=行, 1=列 */
    int wrap;              /* 是否换行 */
    int justify;           /* 主轴对齐 */
    int align_items;       /* 交叉轴对齐 */

    /* 锚点参数 */
    int anchor_flags;
    int offset_left, offset_right;
    int offset_top, offset_bottom;
} fr_layout_t;

/* 布局操作 */
fr_layout_t *fr_layout_create(int type);
void fr_layout_destroy(fr_layout_t *layout);
void fr_layout_apply(fr_layout_t *layout, fr_widget_t *container);

/* 设置布局参数 */
void fr_layout_set_spacing(fr_layout_t *layout, int spacing);
void fr_layout_set_margin(fr_layout_t *layout, int margin);
void fr_layout_set_grid(fr_layout_t *layout, int cols, int rows);
void fr_layout_set_anchor(fr_layout_t *layout, int flags,
                          int left, int top, int right, int bottom);
void fr_layout_set_flex(fr_layout_t *layout, int direction, int wrap,
                        int justify, int align_items);

#endif /* FR_LAYOUT_H */
