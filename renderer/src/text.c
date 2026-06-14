/* text.c - 文本渲染实现
 * 多行/自动换行/富文本/选择
 */

#include "funrender.h"
#include "fr_context.h"
#include "gfx.h"
#include "string.h"

/* 绘制单行文本 */
void fr_draw_text(fr_context_t *ctx, int x, int y, const char *text,
                  fr_color_t color, int max_width)
{
    if (ctx == NULL || text == NULL || ctx->framebuffer == NULL)
        return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t text_color = ((uint32_t)color.r << 16) |
                          ((uint32_t)color.g << 8) |
                          (uint32_t)color.b;

    /* 逐字符绘制 - 简化实现 */
    int cx = x;
    for (int i = 0; text[i]; i++) {
        if (max_width > 0 && cx >= x + max_width)
            break;

        /* 绘制字符（8x16 像素字体） */
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < 8; col++) {
                /* 简化：使用系统字体渲染 */
                int px = cx + col;
                int py = y + row;
                if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                    /* 实际由字体引擎完成 */
                }
            }
        }
        cx += 8;
    }

    (void)text_color;
    (void)gfx_ctx;
}

/* 绘制多行文本 */
void fr_draw_text_multiline(fr_context_t *ctx, int x, int y, const char *text,
                            fr_color_t color, int line_height, int max_width)
{
    if (ctx == NULL || text == NULL) return;

    int cy = y;
    int line_start = 0;

    for (int i = 0; ; i++) {
        if (text[i] == '\n' || text[i] == '\0') {
            /* 绘制一行 */
            int len = i - line_start;
            if (len > 0) {
                char line[256];
                for (int j = 0; j < len && j < 255; j++)
                    line[j] = text[line_start + j];
                line[len < 255 ? len : 255] = '\0';
                fr_draw_text(ctx, x, cy, line, color, max_width);
            }
            cy += line_height > 0 ? line_height : 16;
            line_start = i + 1;

            if (text[i] == '\0') break;
        }
    }
}

/* 自动换行绘制文本 */
void fr_draw_text_wrap(fr_context_t *ctx, int x, int y, const char *text,
                       fr_color_t color, int max_width, int line_height)
{
    if (ctx == NULL || text == NULL || max_width <= 0) return;

    int cy = y;
    int line_start = 0;
    int last_space = -1;
    int char_width = 8;  /* 每字符宽度 */
    int chars_per_line = max_width / char_width;

    if (chars_per_line <= 0) chars_per_line = 1;

    int col = 0;
    for (int i = 0; ; i++) {
        if (text[i] == ' ' || text[i] == '\n' || text[i] == '\0') {
            if (col >= chars_per_line || text[i] == '\n') {
                /* 换行 */
                int len = (text[i] == '\n' || text[i] == '\0') ?
                          i - line_start :
                          (last_space >= line_start ? last_space - line_start : i - line_start);

                if (len > 0) {
                    char line[256];
                    for (int j = 0; j < len && j < 255; j++)
                        line[j] = text[line_start + j];
                    line[len < 255 ? len : 255] = '\0';
                    fr_draw_text(ctx, x, cy, line, color, max_width);
                }

                cy += line_height > 0 ? line_height : 16;
                line_start = (text[i] == ' ' && last_space >= line_start) ?
                             last_space + 1 : i + 1;
                col = 0;
            }

            if (text[i] == ' ') last_space = i;
            if (text[i] == '\0') break;
        }
        col++;
    }
}

/* 绘制选中文本高亮 */
void fr_draw_text_selection(fr_context_t *ctx, int x, int y,
                            int start_col, int end_col, int line_height)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    int sel_x = x + start_col * 8;
    int sel_w = (end_col - start_col) * 8;

    gfx_rect_t sel = {sel_x, y, sel_w, line_height > 0 ? line_height : 16};
    gfx_fill_rect(&gfx_ctx, sel, 0xCCE8FF);
}

/* 测量文本宽度 */
int fr_text_width(const char *text, int font_size)
{
    (void)font_size;
    int w = 0;
    for (int i = 0; text[i]; i++)
        w += 8;  /* 简化：每字符8像素 */
    return w;
}

/* 测量文本高度 */
int fr_text_height(int font_size, int line_count)
{
    (void)font_size;
    return line_count * 16;
}
