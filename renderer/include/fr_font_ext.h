/* fr_font_ext.h - 扩展字体系统
 * 提供多字号、粗体、斜体、下划线、删除线、
 * 文本对齐、自动换行、文本测量、字体度量和富文本支持
 */

#ifndef FR_FONT_EXT_H
#define FR_FONT_EXT_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;

/* ---- 字体度量 ---- */

/* 字体度量结构 */
typedef struct {
    int ascent;           /* 上升高度 (基线以上) */
    int descent;          /* 下降高度 (基线以下) */
    int line_height;      /* 行高 (ascent + descent + 行间距) */
    int x_height;         /* x 高度 (小写字母高度) */
    int cap_height;       /* 大写字母高度 */
    int avg_char_width;   /* 平均字符宽度 */
    int max_char_width;   /* 最大字符宽度 */
    int underscore_pos;   /* 下划线位置 (距基线) */
    int underscore_thick; /* 下划线厚度 */
    int strikethrough_pos;/* 删除线位置 (距基线) */
} fr_font_metrics_t;

/* ---- 字形结构 ---- */

/* 字形位图 (简化，使用 1bpp 位图) */
#define FR_GLYPH_MAX_WIDTH   32
#define FR_GLYPH_MAX_HEIGHT  32

typedef struct {
    uint32_t codepoint;           /* Unicode 码点 */
    int width;                    /* 字形宽度 (像素) */
    int height;                   /* 字形高度 (像素) */
    int bearing_x;                /* 左侧 bearings */
    int bearing_y;                /* 顶部 bearing */
    int advance;                  /* 前进宽度 (像素) */
    uint8_t bitmap[FR_GLYPH_MAX_WIDTH * FR_GLYPH_MAX_HEIGHT / 8]; /* 1bpp 位图 */
} fr_glyph_t;

/* ---- 字体面 ---- */

/* 字形缓存大小 */
#define FR_FONT_CACHE_SIZE  256

/* 字体面结构 */
typedef struct {
    char family[64];               /* 字体族名 */
    int base_size;                 /* 基础字号 (pt) */
    fr_font_metrics_t metrics;     /* 字体度量 */
    fr_glyph_t *glyphs;           /* 字形数组 */
    uint32_t glyph_count;         /* 字形数量 */

    /* 快速查找缓存 */
    fr_glyph_t *cache[FR_FONT_CACHE_SIZE];
} fr_font_face_t;

/* ---- 文本样式 ---- */

/* 文本装饰标志 */
#define FR_TEXT_DECOR_NONE          0x00
#define FR_TEXT_DECOR_UNDERLINE     0x01  /* 下划线 */
#define FR_TEXT_DECOR_STRIKETHROUGH 0x02  /* 删除线 */
#define FR_TEXT_DECOR_OVERLINE      0x04  /* 上划线 */

/* 文本样式 */
typedef struct {
    int font_size;              /* 字号 */
    fr_color_t color;           /* 文字颜色 */
    uint8_t decor_flags;        /* 装饰标志 */
    uint8_t bold;               /* 粗体 */
    uint8_t italic;             /* 斜体 */
} fr_text_style_t;

/* 文本对齐 */
#define FR_TEXT_ALIGN_LEFT      0
#define FR_TEXT_ALIGN_CENTER    1
#define FR_TEXT_ALIGN_RIGHT     2
#define FR_TEXT_ALIGN_JUSTIFY   3  /* 两端对齐 (暂未实现) */

/* ---- 富文本 ---- */

/* 富文本样式运行 */
typedef struct {
    int start;                  /* 起始字符索引 */
    int end;                    /* 结束字符索引 (不含) */
    fr_text_style_t style;      /* 该段的样式 */
} fr_rich_text_run_t;

/* 富文本块 */
#define FR_RICH_TEXT_MAX_RUNS  32

typedef struct {
    const char *text;                      /* 文本内容 */
    int text_len;                          /* 文本长度 */
    fr_rich_text_run_t runs[FR_RICH_TEXT_MAX_RUNS];
    uint32_t run_count;
} fr_rich_text_t;

/* ---- 换行结果 ---- */

/* 单行信息 */
typedef struct {
    int start;                  /* 本行起始字符索引 */
    int end;                    /* 本行结束字符索引 (不含) */
    int width_px;               /* 本行像素宽度 */
} fr_text_line_t;

/* 换行结果 */
#define FR_MAX_TEXT_LINES       128

typedef struct {
    fr_text_line_t lines[FR_MAX_TEXT_LINES];
    uint32_t line_count;
    int total_height;           /* 总像素高度 */
    int max_width;              /* 最大行宽 */
} fr_text_layout_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* ---- 字体面管理 ---- */

/* 创建字体面 */
fr_font_face_t *fr_font_face_create(const char *family, int base_size,
                                    const fr_font_metrics_t *metrics);

/* 销毁字体面 */
void fr_font_face_destroy(fr_font_face_t *face);

/* 设置字形表 (批量加载) */
void fr_font_face_set_glyphs(fr_font_face_t *face,
                             fr_glyph_t *glyphs, uint32_t count);

/* 查找指定码点的字形 */
fr_glyph_t *fr_font_face_find_glyph(fr_font_face_t *face, uint32_t codepoint);

/* ---- 字形缩放 ---- */

/* 将字形缩放到指定尺寸 (生成临时缩放位图) */
void fr_glyph_scale(const fr_glyph_t *src, int src_size,
                    int dst_size, fr_glyph_t *dst);

/* ---- 字体度量 ---- */

/* 获取字体度量 */
const fr_font_metrics_t *fr_font_get_metrics(const fr_font_face_t *face);

/* 获取指定字号的缩放后度量 */
void fr_font_get_scaled_metrics(const fr_font_face_t *face, int font_size,
                                fr_font_metrics_t *out_metrics);

/* ---- 文本绘制 ---- */

/* 绘制文本 (支持样式) */
void fr_font_draw_text(struct fr_context *ctx,
                       int x, int y, const char *text, int len,
                       const fr_font_face_t *face,
                       const fr_text_style_t *style);

/* 绘制粗体文本 */
void fr_font_draw_text_bold(struct fr_context *ctx,
                            int x, int y, const char *text, int len,
                            const fr_font_face_t *face,
                            const fr_text_style_t *style);

/* 绘制斜体文本 */
void fr_font_draw_text_italic(struct fr_context *ctx,
                              int x, int y, const char *text, int len,
                              const fr_font_face_t *face,
                              const fr_text_style_t *style);

/* 绘制文本装饰 (下划线/删除线/上划线) */
void fr_font_draw_decorations(struct fr_context *ctx,
                              int x, int y, int width, int height,
                              uint8_t decor_flags, fr_color_t color,
                              const fr_font_metrics_t *metrics);

/* ---- 文本对齐 ---- */

/* 计算对齐后的 X 坐标 */
int fr_font_align_x(int container_x, int container_w,
                    int text_width, int alignment);

/* 绘制带对齐的文本 */
void fr_font_draw_text_aligned(struct fr_context *ctx,
                               int x, int y, int w, int h,
                               const char *text, int len,
                               const fr_font_face_t *face,
                               const fr_text_style_t *style,
                               int halign);

/* ---- 文本换行 ---- */

/* 计算文本换行布局 */
void fr_font_layout_text(const char *text, int len,
                         const fr_font_face_t *face,
                         int font_size, int max_width,
                         fr_text_layout_t *layout);

/* 绘制换行后的文本 */
void fr_font_draw_text_wrapped(struct fr_context *ctx,
                               int x, int y, int max_width,
                               const char *text, int len,
                               const fr_font_face_t *face,
                               const fr_text_style_t *style,
                               int halign);

/* ---- 文本测量 ---- */

/* 测量文本像素宽度 */
int fr_font_measure_text(const fr_font_face_t *face,
                         const char *text, int len, int font_size);

/* 测量文本像素高度 (考虑换行) */
int fr_font_measure_height(const fr_font_face_t *face,
                           const char *text, int len,
                           int font_size, int max_width);

/* 测量单个字符的宽度 */
int fr_font_measure_char(const fr_font_face_t *face, char c, int font_size);

/* ---- 富文本 ---- */

/* 创建富文本块 */
void fr_rich_text_init(fr_rich_text_t *rt, const char *text, int len);

/* 向富文本块添加样式运行 */
int fr_rich_text_add_run(fr_rich_text_t *rt, int start, int end,
                         const fr_text_style_t *style);

/* 绘制富文本 */
void fr_rich_text_draw(struct fr_context *ctx,
                       int x, int y, int max_width,
                       const fr_rich_text_t *rt,
                       const fr_font_face_t *face,
                       int halign);

/* 测量富文本的宽度和高度 */
void fr_rich_text_measure(const fr_rich_text_t *rt,
                          const fr_font_face_t *face,
                          int max_width,
                          int *out_width, int *out_height);

#endif /* FR_FONT_EXT_H */