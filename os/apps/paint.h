/* paint.h - FUNSOS 画图应用
 * 简易位图绘制工具，支持画笔、形状、颜色选择
 */

#ifndef PAINT_H
#define PAINT_H

#include "stdint.h"

/* 工具类型 */
#define PAINT_TOOL_PENCIL    0  /* 铅笔 */
#define PAINT_TOOL_BRUSH     1  /* 画笔 */
#define PAINT_TOOL_ERASER    2  /* 橡皮擦 */
#define PAINT_TOOL_LINE      3  /* 直线 */
#define PAINT_TOOL_RECT      4  /* 矩形 */
#define PAINT_TOOL_FILL_RECT 5  /* 填充矩形 */
#define PAINT_TOOL_OVAL      6  /* 椭圆 */
#define PAINT_TOOL_FILL_OVAL 7  /* 填充椭圆 */
#define PAINT_TOOL_FILL      8  /* 填充 */
#define PAINT_TOOL_TEXT      9  /* 文字 */
#define PAINT_TOOL_PICKER    10 /* 取色器 */

/* 画布最大尺寸 */
#define PAINT_MAX_WIDTH  1920
#define PAINT_MAX_HEIGHT 1080
#define PAINT_DEFAULT_WIDTH  800
#define PAINT_DEFAULT_HEIGHT 600

/* 颜色定义 */
typedef uint32_t paint_color_t;

#define PAINT_COLOR_BLACK   0xFF000000
#define PAINT_COLOR_WHITE   0xFFFFFFFF
#define PAINT_COLOR_RED     0xFF0000FF
#define PAINT_COLOR_GREEN   0xFF00FF00
#define PAINT_COLOR_BLUE    0xFFFF0000
#define PAINT_COLOR_YELLOW  0xFF00FFFF
#define PAINT_COLOR_CYAN    0xFFFFFF00
#define PAINT_COLOR_MAGENTA 0xFFFF00FF
#define PAINT_COLOR_GRAY    0xFF808080

/* 画笔样式 */
typedef struct {
    uint32_t size;       /* 画笔大小 (1-50) */
    uint32_t hardness;   /* 硬度 (0-100) */
    uint32_t opacity;    /* 不透明度 (0-255) */
    uint32_t spacing;    /* 间距 (1-100) */
    uint8_t  anti_aliased; /* 是否抗锯齿 */
} paint_brush_t;

/* 图层 */
typedef struct {
    uint32_t width;
    uint32_t height;
    paint_color_t *pixels;  /* 像素数据 */
    uint8_t  visible;       /* 是否可见 */
    uint8_t  locked;        /* 是否锁定 */
    float    opacity;       /* 不透明度 (0.0-1.0) */
    char     name[32];      /* 图层名称 */
} paint_layer_t;

/* 绘图状态 */
typedef struct {
    uint32_t    tool;           /* 当前工具 */
    paint_color_t fg_color;     /* 前景色 */
    paint_color_t bg_color;     /* 背景色 */
    paint_brush_t brush;        /* 画笔设置 */
    uint32_t    canvas_width;   /* 画布宽度 */
    uint32_t    canvas_height;  /* 画布高度 */
    paint_layer_t *layers;      /* 图层数组 */
    uint32_t    layer_count;    /* 图层数量 */
    uint32_t    active_layer;   /* 当前活动图层 */
    int32_t     start_x;        /* 拖拽起始 X */
    int32_t     start_y;        /* 拖拽起始 Y */
    int32_t     last_x;         /* 上次绘制 X */
    int32_t     last_y;         /* 上次绘制 Y */
    uint8_t     is_drawing;     /* 是否正在绘制 */
    uint8_t     modified;       /* 是否有未保存修改 */
    uint8_t     show_grid;      /* 是否显示网格 */
    uint32_t    grid_size;      /* 网格大小 */
    char        filename[256];  /* 关联文件名 */
} paint_state_t;

/* 初始化画图应用 */
int paint_init(void);

/* 运行画图主循环 */
void paint_run(void);

/* 设置工具 */
void paint_set_tool(uint32_t tool);

/* 设置颜色 */
void paint_set_fg_color(paint_color_t color);
void paint_set_bg_color(paint_color_t color);

/* 设置画笔大小 */
void paint_set_brush_size(uint32_t size);

/* 获取当前状态 */
const paint_state_t *paint_get_state(void);

/* 新建画布 */
int paint_new_canvas(uint32_t w, uint32_t h);

/* 保存图像 */
int paint_save(const char *filename);

/* 加载图像 */
int paint_load(const char *filename);

/* 撤销 */
int paint_undo(void);

/* 重做 */
int paint_redo(void);

/* 关闭画图 */
void paint_close(void);

#endif /* PAINT_H */