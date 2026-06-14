/* fr_context.h - 渲染上下文
 * 管理渲染引擎的全局状态和帧缓冲
 */

#ifndef FR_CONTEXT_H
#define FR_CONTEXT_H

#include "stdint.h"

/* Forward declaration for fr_color_t (defined in funrender.h) */
#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 渲染上下文 */
typedef struct fr_context {
    uint32_t *framebuffer;      /* 帧缓冲指针 */
    int width;                  /* 画布宽度 */
    int height;                 /* 画布高度 */
    int pitch;                  /* 每行字节数 */
    int bpp;                    /* 每像素位数 */

    /* 裁剪区域 */
    int clip_x, clip_y;
    int clip_w, clip_h;

    /* 当前主题 */
    void *current_theme;

    /* 根控件 */
    void *root_widget;

    /* 脏区域标记 */
    uint8_t dirty;
    int dirty_x, dirty_y, dirty_w, dirty_h;

    /* 字体信息 */
    char font_name[64];
    int font_size;

    /* 动画列表 */
    void *animations;

    /* 统计信息 */
    uint32_t frame_count;
    uint32_t last_fps;
} fr_context_t;

/* 上下文操作 */
fr_context_t *fr_context_create(int width, int height, void *framebuffer);
void fr_context_destroy(fr_context_t *ctx);
void fr_context_resize(fr_context_t *ctx, int width, int height);

/* 裁剪操作 */
void fr_context_set_clip(fr_context_t *ctx, int x, int y, int w, int h);
void fr_context_reset_clip(fr_context_t *ctx);

/* 帧缓冲操作 */
void fr_context_clear(fr_context_t *ctx, fr_color_t color);
void fr_context_present(fr_context_t *ctx);

/* 标记脏区域 */
void fr_context_mark_dirty(fr_context_t *ctx, int x, int y, int w, int h);

/* 内存分配辅助函数（使用内核 kmalloc/kfree） */
void *fr_alloc(uint32_t size);
void fr_free(void *ptr);

#endif /* FR_CONTEXT_H */
