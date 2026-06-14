/* context.c - 渲染上下文实现
 * 管理帧缓冲、裁剪区域和渲染状态
 */

#include "fr_context.h"
#include "funrender.h"
#include "gfx.h"
#include "string.h"
#include "kheap.h"

/* ---- 内存分配辅助函数 ---- */

void *fr_alloc(uint32_t size)
{
    return kmalloc(size);
}

void fr_free(void *ptr)
{
    if (ptr) kfree(ptr);
}

/* 创建渲染上下文 */
fr_context_t *fr_context_create(int width, int height, void *framebuffer)
{
    fr_context_t *ctx = (fr_context_t *)fr_alloc(sizeof(fr_context_t));
    if (ctx == NULL)
        return NULL;

    ctx->framebuffer = (uint32_t *)framebuffer;
    ctx->width = width;
    ctx->height = height;
    ctx->pitch = width * 4;
    ctx->bpp = 32;

    /* 默认裁剪区域为全屏 */
    ctx->clip_x = 0;
    ctx->clip_y = 0;
    ctx->clip_w = width;
    ctx->clip_h = height;

    ctx->current_theme = NULL;
    ctx->root_widget = NULL;
    ctx->dirty = 1;
    ctx->dirty_x = 0;
    ctx->dirty_y = 0;
    ctx->dirty_w = width;
    ctx->dirty_h = height;

    ctx->font_name[0] = '\0';
    ctx->font_size = 14;
    ctx->animations = NULL;
    ctx->frame_count = 0;
    ctx->last_fps = 0;

    return ctx;
}

/* 销毁渲染上下文 */
void fr_context_destroy(fr_context_t *ctx)
{
    if (ctx == NULL)
        return;
    fr_free(ctx);
}

/* 调整上下文大小 */
void fr_context_resize(fr_context_t *ctx, int width, int height)
{
    if (ctx == NULL)
        return;

    ctx->width = width;
    ctx->height = height;
    ctx->pitch = width * 4;
    ctx->clip_w = width;
    ctx->clip_h = height;
    ctx->dirty = 1;
    ctx->dirty_w = width;
    ctx->dirty_h = height;
}

/* 设置裁剪区域 */
void fr_context_set_clip(fr_context_t *ctx, int x, int y, int w, int h)
{
    if (ctx == NULL)
        return;

    ctx->clip_x = x;
    ctx->clip_y = y;
    ctx->clip_w = w;
    ctx->clip_h = h;
}

/* 重置裁剪区域 */
void fr_context_reset_clip(fr_context_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->clip_x = 0;
    ctx->clip_y = 0;
    ctx->clip_w = ctx->width;
    ctx->clip_h = ctx->height;
}

/* 清除帧缓冲 */
void fr_context_clear(fr_context_t *ctx, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL)
        return;

    uint32_t pixel = ((uint32_t)color.r << 16) |
                     ((uint32_t)color.g << 8) |
                     (uint32_t)color.b;

    for (int y = ctx->clip_y; y < ctx->clip_y + ctx->clip_h && y < ctx->height; y++) {
        for (int x = ctx->clip_x; x < ctx->clip_x + ctx->clip_w && x < ctx->width; x++) {
            ctx->framebuffer[y * ctx->width + x] = pixel;
        }
    }
}

/* 呈现帧缓冲（交换缓冲区） */
void fr_context_present(fr_context_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->frame_count++;
    ctx->dirty = 0;
}

/* 标记脏区域 */
void fr_context_mark_dirty(fr_context_t *ctx, int x, int y, int w, int h)
{
    if (ctx == NULL)
        return;

    if (!ctx->dirty) {
        ctx->dirty_x = x;
        ctx->dirty_y = y;
        ctx->dirty_w = w;
        ctx->dirty_h = h;
        ctx->dirty = 1;
    } else {
        /* 合并脏区域 */
        int x1 = ctx->dirty_x < x ? ctx->dirty_x : x;
        int y1 = ctx->dirty_y < y ? ctx->dirty_y : y;
        int x2 = (ctx->dirty_x + ctx->dirty_w) > (x + w) ?
                 (ctx->dirty_x + ctx->dirty_w) : (x + w);
        int y2 = (ctx->dirty_y + ctx->dirty_h) > (y + h) ?
                 (ctx->dirty_y + ctx->dirty_h) : (y + h);

        ctx->dirty_x = x1;
        ctx->dirty_y = y1;
        ctx->dirty_w = x2 - x1;
        ctx->dirty_h = y2 - y1;
    }
}

/* ================================================================
 *  核心渲染 API
 * ================================================================ */

/* 初始化渲染引擎 */
fr_handle_t fr_init(int width, int height, void *framebuffer)
{
    fr_context_t *ctx = fr_context_create(width, height, framebuffer);
    if (ctx == NULL)
        return NULL;

    /* 初始化主题系统 */
    fr_theme_system_init();

    /* 初始化事件系统 */
    fr_event_system_init((fr_handle_t)ctx);

    return (fr_handle_t)ctx;
}

/* 关闭渲染引擎 */
void fr_shutdown(fr_handle_t ctx)
{
    if (ctx == NULL) return;

    fr_context_t *c = (fr_context_t *)ctx;

    /* 销毁根控件及其子控件 */
    if (c->root_widget) {
        fr_destroy_widget(c->root_widget);
    }

    fr_event_system_shutdown(ctx);
    fr_context_destroy(c);
}

/* 渲染一帧 */
void fr_render(fr_handle_t ctx)
{
    fr_context_t *c = (fr_context_t *)ctx;
    if (c == NULL) return;

    /* 清除帧缓冲 */
    fr_context_clear(c, FR_COLOR_WHITE);

    /* 递归渲染所有控件 */
    fr_widget_t *widget = (fr_widget_t *)c->root_widget;
    while (widget) {
        if (widget->state & FR_STATE_VISIBLE && widget->render) {
            widget->render(widget, c);
        }
        widget = widget->first_child;
    }

    /* 呈现帧缓冲 */
    fr_context_present(c);
}

/* 标记控件需要重绘 */
void fr_invalidate(fr_handle_t widget)
{
    fr_widget_t *w = (fr_widget_t *)widget;
    if (w == NULL) return;
    w->state |= FR_STATE_DIRTY;
}

/* 标记所有控件需要重绘 */
void fr_invalidate_all(fr_handle_t ctx)
{
    fr_context_t *c = (fr_context_t *)ctx;
    if (c == NULL) return;
    c->dirty = 1;
    c->dirty_x = 0;
    c->dirty_y = 0;
    c->dirty_w = c->width;
    c->dirty_h = c->height;
}
