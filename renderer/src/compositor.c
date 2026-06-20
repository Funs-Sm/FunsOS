/*
 * compositor.c - 窗口合成器实现
 *
 * 管理多个重叠表面 (Surface) 的合成与渲染:
 *   - Z-Order 排序: 按深度顺序管理所有可见窗口
 *   - Alpha 混合: 支持每个表面的独立透明度
 *   - 脏区域跟踪: 只重绘发生变化的部分区域, 提高渲染效率
 *   - 硬件加速提示: 标记可利用 GPU 加速的合成操作
 */

#include "funrender.h"
#include "fr_compositor.h"
#include "fr_context.h"
#include "gfx.h"
#include "string.h"

/* ---- 辅助函数 ---- */

/* Alpha 混合两个像素值 (标准 Porter-Duff Source Over) */
static uint32_t blend_pixel_src_over(uint32_t src, uint32_t dst, uint8_t alpha) {
    if (alpha == 0) return dst;
    if (alpha == 255) return src;

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint32_t inv_alpha = 255 - alpha;
    uint8_t r = (uint8_t)((sr * alpha + dr * inv_alpha) / 255);
    uint8_t g = (uint8_t)((sg * alpha + dg * inv_alpha) / 255);
    uint8_t b = (uint8_t)((sb * alpha + db * inv_alpha) / 255);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* 加法混合模式 */
static uint32_t blend_pixel_additive(uint32_t src, uint32_t dst, uint8_t alpha) {
    if (alpha == 0) return dst;

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t r = (uint8_t)(sr * alpha / 255 + dr);
    if (r < dr) r = 255; /* 饱和溢出 */
    uint8_t g = (uint8_t)(sg * alpha / 255 + dg);
    if (g < dg) g = 255;
    uint8_t b = (uint8_t)(sb * alpha / 255 + db);
    if (b < db) b = 255;

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* 正片叠底混合模式 */
static uint32_t blend_pixel_multiply(uint32_t src, uint32_t dst, uint8_t alpha) {
    if (alpha == 0) return dst;

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t r = (uint8_t)((sr * dr / 255) * alpha / 255 + dr * (255 - alpha) / 255);
    uint8_t g = (uint8_t)((sg * dg / 255) * alpha / 255 + dg * (255 - alpha) / 255);
    uint8_t b = (uint8_t)((sb * db / 255) * alpha / 255 + db * (255 - alpha) / 255);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* 判断两个矩形是否相交 */
static int rects_intersect(int x1, int y1, int w1, int h1,
                           int x2, int y2, int w2, int h2) {
    return !(x1 + w1 <= x2 || x2 + w2 <= x1 ||
             y1 + h1 <= y2 || y2 + h2 <= y1);
}

/* 将一个表面插入到 Z 序链表的正确位置 */
static void z_list_insert(fr_compositor_t *comp, fr_surface_t *surf) {
    fr_surface_t **pp = &comp->z_list_head;
    while (*pp && (*pp)->z_order <= surf->z_order) {
        pp = &(*pp)->next;
    }
    surf->next = *pp;
    surf->prev = (*pp == NULL) ? comp->z_list_tail :
                 (fr_surface_t *)((char *)*pp - offsetof(fr_surface_t, next));
    if (*pp) (*pp)->prev = surf;
    else comp->z_list_tail = surf;
    *pp = surf;
    if (surf->prev == NULL && comp->z_list_head != surf)
        surf->prev = NULL; /* 插入到链表头 */
    if (comp->z_list_head == surf)
        surf->prev = NULL; /* 头节点前驱为空 */

    /* 简化处理: 直接重建 prev 链接 */
    if (comp->z_list_head == surf) {
        surf->prev = NULL;
    }
}

/* 从 Z 序链表中移除一个表面 */
static void z_list_remove(fr_surface_t *surf) {
    if (surf->prev) surf->prev->next = surf->next;
    if (surf->next) surf->next->prev = surf->prev;
}

/* ---- 公共 API 实现 ---- */

/*
 * fr_compositor_init - 初始化窗口合成器
 *
 * 分配表面管理数组, 清空脏区域, 设置帧缓冲信息。
 */
void fr_compositor_init(fr_compositor_t *comp, uint32_t *fb,
                         int width, int height, int pitch) {
    if (comp == NULL) return;

    comp->surfaces = (fr_surface_t *)fr_alloc(
        sizeof(fr_surface_t) * FR_MAX_SURFACES);
    if (comp->surfaces) {
        memset(comp->surfaces, 0,
               sizeof(fr_surface_t) * FR_MAX_SURFACES);
    }

    comp->surface_count = 0;
    comp->max_surfaces = FR_MAX_SURFACES;
    comp->z_list_head = NULL;
    comp->z_list_tail = NULL;
    comp->next_id = 1;

    memset(comp->damage_rects, 0, sizeof(comp->damage_rects));
    comp->damage_count = 0;
    comp->damage_fullscreen = 1; /* 初始全屏为脏，确保首次完整绘制 */

    comp->framebuffer = fb;
    comp->fb_width = width;
    comp->fb_height = height;
    comp->fb_pitch = pitch > 0 ? pitch : width * 4;

    comp->composite_count = 0;
    comp->blend_pixel_count = 0;
    comp->skip_count = 0;

    comp->hw_accel_available = 0;  /* 默认不假设硬件加速可用 */
    comp->hw_accel_enabled = 0;
}

/*
 * fr_compositor_shutdown - 销毁合成器
 *
 * 释放所有分配的内存资源。
 */
void fr_compositor_shutdown(fr_compositor_t *comp) {
    if (comp == NULL) return;

    if (comp->surfaces) {
        fr_free(comp->surfaces);
        comp->surfaces = NULL;
    }
    comp->surface_count = 0;
    comp->z_list_head = NULL;
    comp->z_list_tail = NULL;
}

/*
 * fr_compositor_add_surface - 创建并添加新表面
 *
 * 分配一个新的表面槽位, 初始化其属性并加入 Z 序。
 * 返回新创建的表面指针, 或 NULL 表示失败。
 */
fr_surface_t *fr_compositor_add_surface(fr_compositor_t *comp,
                                          uint32_t type,
                                          int x, int y, int w, int h) {
    if (comp == NULL || comp->surfaces == NULL) return NULL;
    if (comp->surface_count >= comp->max_surfaces) return NULL;

    /* 找一个空闲槽位 */
    fr_surface_t *surf = NULL;
    for (uint32_t i = 0; i < comp->max_surfaces; i++) {
        if (comp->surfaces[i].id == 0) {
            surf = &comp->surfaces[i];
            break;
        }
    }
    if (surf == NULL) return NULL;

    /* 初始化表面属性 */
    surf->id = comp->next_id++;
    surf->type = type;
    surf->x = x;
    surf->y = y;
    surf->w = w;
    surf->h = h;
    surf->alpha = 255;
    surf->z_order = (int)surf->id;  /* 默认按添加顺序排列 */
    surf->composite_mode = FR_COMPOSITE_SRC_OVER;
    surf->buffer = NULL;
    surf->pitch = w * 4;
    surf->bpp = 32;
    surf->flags = FR_SURFACE_VISIBLE;
    surf->user_data = NULL;
    surf->prev = NULL;
    surf->next = NULL;

    /* 插入到 Z 序链表 */
    z_list_insert(comp, surf);

    comp->surface_count++;

    /* 标记此区域的脏矩形 */
    fr_compositor_damage_add(comp, x, y, w, h);

    return surf;
}

/*
 * fr_compositor_remove_surface - 移除指定表面
 *
 * 通过 ID 找到对应表面, 从 Z 序中移除并释放槽位。
 * 返回 0=成功, -1=未找到。
 */
int fr_compositor_remove_surface(fr_compositor_t *comp, uint32_t surface_id) {
    if (comp == NULL) return -1;

    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return -1;

    /* 记录被移除表面的位置用于脏区域标记 */
    int old_x = surf->x, old_y = surf->y;
    int old_w = surf->w, old_h = surf->h;

    /* 从 Z 序链表中移除 */
    z_list_remove(surf);

    /* 清空槽位 */
    memset(surf, 0, sizeof(fr_surface_t));
    comp->surface_count--;

    /* 被移除的区域需要重绘底层内容 */
    fr_compositor_damage_add(comp, old_x, old_y, old_w, old_h);

    return 0;
}

/*
 * fr_compositor_find_surface - 通过 ID 查找表面
 */
fr_surface_t *fr_compositor_find_surface(fr_compositor_t *comp, uint32_t surface_id) {
    if (comp == NULL || comp->surfaces == NULL) return NULL;

    for (uint32_t i = 0; i < comp->max_surfaces; i++) {
        if (comp->surfaces[i].id == surface_id)
            return &comp->surfaces[i];
    }
    return NULL;
}

/*
 * fr_compositor_set_zorder - 设置表面的 Z 序值
 *
 * 更新后重新排序 Z 序链表。
 */
void fr_compositor_set_zorder(fr_compositor_t *comp,
                               uint32_t surface_id, int z_order) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;

    z_list_remove(surf);
    surf->z_order = z_order;
    z_list_insert(comp, surf);

    surf->flags |= FR_SURFACE_DAMAGE;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
}

/*
 * fr_compositor_composite - 执行合成操作
 *
 * 按 Z 序从底到顶遍历所有可见表面:
 *   - 如果是全屏脏或表面有脏标志, 则完全重绘该表面
 *   - 否则只重绘与脏区域相交的部分
 *   - 使用对应的混合模式进行像素级 Alpha 混合
 */
void fr_compositor_composite(fr_compositor_t *comp) {
    if (comp == NULL || comp->framebuffer == NULL) return;
    if (comp->z_list_head == NULL) return;

    comp->composite_count++;

    /*
     * 如果全屏脏标记已设置, 或者没有脏区域信息,
     * 则执行完整的全量合成。
     */
    int do_full = comp->damage_fullscreen || comp->damage_count == 0;

    /* 遍历 Z 序链表 (从底层到顶层) */
    fr_surface_t *surf = comp->z_list_head;
    while (surf != NULL) {
        /* 跳过不可见的表面 */
        if (!(surf->flags & FR_SURFACE_VISIBLE) ||
            surf->buffer == NULL) {
            surf = surf->next;
            continue;
        }

        /* 计算需要绘制的区域 */
        int draw_x = surf->x;
        int draw_y = surf->y;
        int draw_w = surf->w;
        int draw_h = surf->h;

        /* 裁剪到屏幕范围 */
        if (draw_x < 0) { draw_w += draw_x; draw_x = 0; }
        if (draw_y < 0) { draw_h += draw_y; draw_y = 0; }
        if (draw_x + draw_w > comp->fb_width) draw_w = comp->fb_width - draw_x;
        if (draw_y + draw_h > comp->fb_height) draw_h = comp->fb_height - draw_y;
        if (draw_w <= 0 || draw_h <= 0) { surf = surf->next; continue; }

        /*
         * 像素级合成循环。
         * 对于每个目标像素, 从源表面取色并与当前帧缓冲混合。
         * 这是软件光栅化器的核心路径。
         */
        for (int sy = 0; sy < draw_h; sy++) {
            int screen_y = draw_y + sy;
            for (int sx = 0; sx < draw_w; sx++) {
                int screen_x = draw_x + sx;

                /* 在非全屏模式下, 检查此像素是否在脏区域内 */
                if (!do_full) {
                    int in_damage = 0;
                    for (uint32_t d = 0; d < comp->damage_count; d++) {
                        if (screen_x >= comp->damage_rects[d].x &&
                            screen_x < comp->damage_rects[d].x + comp->damage_rects[d].w &&
                            screen_y >= comp->damage_rects[d].y &&
                            screen_y < comp->damage_rects[d].y + comp->damage_rects[d].h) {
                            in_damage = 1;
                            break;
                        }
                    }
                    if (!in_damage) {
                        comp->skip_count++;
                        continue;
                    }
                }

                /* 从源表面读取像素 */
                uint32_t src_pitch_words = surf->pitch / 4;
                uint32_t src_pixel = surf->buffer[sy * src_pitch_words + sx];

                /* 读取目标帧缓冲中的当前像素 */
                uint32_t dst_pitch_words = comp->fb_pitch / 4;
                uint32_t dst_pixel =
                    comp->framebuffer[screen_y * dst_pitch_words + screen_x];

                /* 根据合成模式选择混合算法 */
                uint32_t result;
                switch (surf->composite_mode) {
                case FR_COMPOSITE_SRC_OVER:
                    result = blend_pixel_src_over(src_pixel, dst_pixel,
                                                   surf->alpha);
                    break;
                case FR_COMPOSITE_ADDITIVE:
                    result = blend_pixel_additive(src_pixel, dst_pixel,
                                                  surf->alpha);
                    break;
                case FR_COMPOSITE_MULTIPLY:
                    result = blend_pixel_multiply(src_pixel, dst_pixel,
                                                  surf->alpha);
                    break;
                case FR_COMPOSITE_SCREEN:
                    /* Screen 模式: 反转颜色后正片叠底再反转 */
                    result = blend_pixel_additive(
                        ~src_pixel, ~dst_pixel, surf->alpha);
                    result = ~result;
                    break;
                default:
                    result = blend_pixel_src_over(src_pixel, dst_pixel,
                                                   surf->alpha);
                    break;
                }

                comp->framebuffer[screen_y * dst_pitch_words + screen_x] = result;
                comp->blend_pixel_count++;
            }
        }

        surf = surf->next;
    }

    /* 合成完成后清除脏区域 */
    fr_compositor_damage_clear(comp);
}

/*
 * fr_compositor_damage_add - 添加脏矩形
 *
 * 将指定矩形区域标记为"脏", 下次合成时只需重绘这些区域。
 * 自动合并相邻/重叠的脏矩形以减少冗余。
 */
void fr_compositor_damage_add(fr_compositor_t *comp,
                               int x, int y, int w, int h) {
    if (comp == NULL) return;
    if (comp->damage_count >= FR_MAX_DAMAGE_RECTS) {
        /* 脏矩形过多时退化为全屏脏 */
        comp->damage_fullscreen = 1;
        return;
    }

    /* 裁剪到屏幕范围 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > comp->fb_width) w = comp->fb_width - x;
    if (y + h > comp->fb_height) h = comp->fb_height - y;

    /* 尝试合并到已有的脏矩形 (简单包围盒合并) */
    for (uint32_t i = 0; i < comp->damage_count; i++) {
        fr_damage_rect_t *r = &comp->damage_rects[i];
        if (rects_intersect(x, y, w, h, r->x, r->y, r->w, r->h)) {
            /* 合并为包围盒 */
            int nx = (x < r->x) ? x : r->x;
            int ny = (y < r->y) ? y : r->y;
            int nx2 = ((x + w) > (r->x + r->w)) ? (x + w) : (r->x + r->w);
            int ny2 = ((y + h) > (r->y + r->h)) ? (y + h) : (r->y + r->h);
            r->x = nx; r->y = ny;
            r->w = nx2 - nx; r->h = ny2 - ny;
            return;
        }
    }

    /* 无法合并则新增一个脏矩形 */
    comp->damage_rects[comp->damage_count].x = x;
    comp->damage_rects[comp->damage_count].y = y;
    comp->damage_rects[comp->damage_count].w = w;
    comp->damage_rects[comp->damage_count].h = h;
    comp->damage_count++;
}

/*
 * fr_compositor_damage_all - 标记整个屏幕为脏
 */
void fr_compositor_damage_all(fr_compositor_t *comp) {
    if (comp == NULL) return;
    comp->damage_fullscreen = 1;
    comp->damage_count = 0;
}

/*
 * fr_compositor_damage_flush - 刷新脏区域
 *
 * 当前实现等同于 composite(), 但可以扩展为:
 * 只将脏区域的内容从各表面拷贝到帧缓冲,
 * 不做完整合成遍历。
 */
void fr_compositor_damage_flush(fr_compositor_t *comp) {
    fr_compositor_composite(comp);
}

/*
 * fr_compositor_damage_clear - 清除所有脏区域标记
 */
void fr_compositor_damage_clear(fr_compositor_t *comp) {
    if (comp == NULL) return;
    comp->damage_count = 0;
    comp->damage_fullscreen = 0;
}

/* ---- 表面属性设置函数 ---- */

void fr_compositor_set_surface_pos(fr_compositor_t *comp,
                                    uint32_t surface_id, int x, int y) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
    surf->x = x;
    surf->y = y;
    fr_compositor_damage_add(comp, x, y, surf->w, surf->h);
}

void fr_compositor_set_surface_size(fr_compositor_t *comp,
                                     uint32_t surface_id, int w, int h) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
    surf->w = w;
    surf->h = h;
    surf->pitch = w * 4;
    fr_compositor_damage_add(comp, surf->x, surf->y, w, h);
}

void fr_compositor_set_surface_alpha(fr_compositor_t *comp,
                                      uint32_t surface_id, uint8_t alpha) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;
    surf->alpha = alpha;
    if (alpha < 255) surf->flags |= FR_SURFACE_OPACITY;
    else surf->flags &= ~FR_SURFACE_OPACITY;
    surf->flags |= FR_SURFACE_DAMAGE;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
}

void fr_compositor_set_surface_visible(fr_compositor_t *comp,
                                        uint32_t surface_id, int visible) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;
    if (visible) surf->flags |= FR_SURFACE_VISIBLE;
    else surf->flags &= ~FR_SURFACE_VISIBLE;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
}

void fr_compositor_set_surface_buffer(fr_compositor_t *comp,
                                       uint32_t surface_id,
                                       uint32_t *buffer, uint32_t pitch) {
    fr_surface_t *surf = fr_compositor_find_surface(comp, surface_id);
    if (surf == NULL) return;
    surf->buffer = buffer;
    surf->pitch = pitch > 0 ? pitch : surf->w * 4;
    surf->flags |= FR_SURFACE_DAMAGE;
    fr_compositor_damage_add(comp, surf->x, surf->y, surf->w, surf->h);
}

/* ---- 统计查询 ---- */

uint32_t fr_compositor_get_damage_count(fr_compositor_t *comp) {
    return comp ? comp->damage_count : 0;
}

int fr_compositor_is_fullscreen_dirty(fr_compositor_t *comp) {
    return comp ? comp->damage_fullscreen : 0;
}

void fr_compositor_set_hw_accel(fr_compositor_t *comp, int enabled) {
    if (comp == NULL) return;
    comp->hw_accel_enabled = enabled ? 1 : 0;
}

/* ================================================================
 *  图层合成优化扩展
 * ================================================================ */

/* ---- 可见区域裁剪 ---- */

/* 整数矩形（用于脏区域追踪和裁剪） */
typedef struct rect_t { int x, y, w, h; } rect_t;

#define DIRTY_RECT_MAX 16

typedef struct {
    rect_t rects[DIRTY_RECT_MAX];
    int count;
    uint64_t frame_counter;
} dirty_tracker_t;

typedef struct {
    fr_surface_t *front;
    fr_surface_t *back;
    int w;
    int h;
    int initialized;
    int needs_swap;
} double_buffer_t;

typedef struct {
    uint64_t blit_count;
    uint64_t blend_count;
    uint64_t flush_count;
    uint64_t pixels_processed;
} comp_stats_t;

/*
 * rect_intersect - 计算两个矩形的交集
 *
 * 返回 1=有交集(结果写入out), 0=无交集。
 */
static int rect_intersect(const rect_t *a, const rect_t *b, rect_t *out)
{
    if (a == NULL || b == NULL || out == NULL) return 0;

    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = ((a->x + a->w) < (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    int y2 = ((a->y + a->h) < (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);

    if (x2 <= x1 || y2 <= y1) return 0;

    out->x = x1; out->y = y1;
    out->w = x2 - x1; out->h = y2 - y1;
    return 1;
}

/*
 * rect_area - 计算矩形面积
 */
static int rect_area(const rect_t *r)
{
    if (r == NULL) return 0;
    return r->w * r->h;
}

/*
 * rect_union - 计算两个矩形的并集
 */
static void rect_union(const rect_t *a, const rect_t *b, rect_t *out)
{
    if (a == NULL || b == NULL || out == NULL) return;

    int x1 = (a->x < b->x) ? a->x : b->x;
    int y1 = (a->y < b->y) ? a->y : b->y;
    int x2 = ((a->x + a->w) > (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    int y2 = ((a->y + a->h) > (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);

    out->x = x1; out->y = y1;
    out->w = x2 - x1; out->h = y2 - y1;
}

/* ---- 脏区域追踪 ---- */

static dirty_tracker_t dirty;

/*
 * compositor_dirty_add - 添加脏矩形到追踪器
 */
void compositor_dirty_add(const rect_t *r)
{
    if (r == NULL) return;

    if (dirty.count >= DIRTY_RECT_MAX) {
        /* 脏矩形过多: 合并为一个大的包围盒 */
        if (dirty.count > 0) {
            /* 计算当前所有脏矩形的包围盒 */
            int min_x = dirty.rects[0].x, min_y = dirty.rects[0].y;
            int max_x = dirty.rects[0].x + dirty.rects[0].w;
            int max_y = dirty.rects[0].y + dirty.rects[0].h;

            for (int i = 1; i < dirty.count; i++) {
                if (dirty.rects[i].x < min_x) min_x = dirty.rects[i].x;
                if (dirty.rects[i].y < min_y) min_y = dirty.rects[i].y;
                int rx = dirty.rects[i].x + dirty.rects[i].w;
                int ry = dirty.rects[i].y + dirty.rects[i].h;
                if (rx > max_x) max_x = rx;
                if (ry > max_y) max_y = ry;
            }

            dirty.count = 1;
            dirty.rects[0].x = min_x; dirty.rects[0].y = min_y;
            dirty.rects[0].w = max_x - min_x; dirty.rects[0].h = max_y - min_y;
        }
        /* 合并新矩形到第一个脏矩形 */
        if (dirty.count > 0) {
            rect_union(&dirty.rects[0], r, &dirty.rects[0]);
        } else {
            dirty.rects[0] = *r;
            dirty.count = 1;
        }
        return;
    }

    /* 尝试与现有脏矩形合并 */
    for (int i = 0; i < dirty.count; i++) {
        rect_t merged;
        if (rect_intersect(&dirty.rects[i], r, &merged)) {
            /* 合并后的面积不能超过两者之和太多 (避免过度膨胀) */
            int area_a = rect_area(&dirty.rects[i]);
            int area_b = rect_area(r);
            int area_merged = rect_area(&merged);

            if (area_merged <= (area_a + area_b) * 3 / 2) {
                dirty.rects[i] = merged;
                return;
            }
        }
    }

    /* 无法合并则新增 */
    dirty.rects[dirty.count++] = *r;
}

/*
 * compositor_dirty_flush - 刷新脏区域到目标表面
 *
 * 将当前累积的脏矩形标记为需要重绘的区域。
 * 实际的重绘由调用方在 composite() 中处理。
 */
void compositor_dirty_flush(fr_surface_t *target)
{
    if (target == NULL) return;
    (void)target; /* 表面指针用于未来硬件加速路径 */

    /* 增加帧计数器 */
    dirty.frame_counter++;
}

/*
 * compositor_dirty_merge - 合并重叠的脏矩形
 *
 * 返回合并后的脏矩形数量。
 */
int compositor_dirty_merge(void)
{
    if (dirty.count <= 1) return dirty.count;

    int merged = 1;
    for (int i = 1; i < dirty.count; i++) {
        int found = 0;
        for (int j = 0; j < merged; j++) {
            rect_t result;
            if (rect_intersect(&dirty.rects[j], &dirty.rects[i], &result)) {
                int area_j = rect_area(&dirty.rects[j]);
                int area_i = rect_area(&dirty.rects[i]);
                int area_r = rect_area(&result);

                if (area_r <= (area_j + area_i) * 4 / 3) {
                    dirty.rects[j] = result;
                    found = 1;
                    break;
                }
            }
        }
        if (!found && merged < DIRTY_RECT_MAX) {
            dirty.rects[merged++] = dirty.rects[i];
        }
    }

    dirty.count = merged;
    return merged;
}

/* ---- 合成模式扩展 ---- */

static int current_blend_mode = FR_COMPOSITE_SRC_OVER;

/* Local composite mode constants not defined in fr_compositor.h */
#define FR_COMPOSITE_OVERLAY   6
#define FR_COMPOSITE_DARKEN    7
#define FR_COMPOSITE_LIGHTEN   8

/*
 * compositor_set_blend_mode - 设置全局合成混合模式
 *
 * 返回之前的模式值。
 */
int compositor_set_blend_mode(int mode)
{
    int prev = current_blend_mode;
    if (mode >= FR_COMPOSITE_SRC_OVER && mode <= FR_COMPOSITE_LIGHTEN) {
        current_blend_mode = mode;
    }
    return prev;
}

/*
 * compositor_blit_layered - 带图层合成模式的位块传输
 *
 * 将源表面以指定混合模式绘制到目标表面的指定位置。
 * 支持 Alpha 通道的多种混合算法。
 *
 * 返回 0=成功, -1=失败。
 */
int compositor_blit_layered(fr_surface_t *dst, fr_surface_t *src,
                             int x, int y, int mode, uint8_t alpha)
{
    if (dst == NULL || src == NULL) return -1;
    if (dst->buffer == NULL || src->buffer == NULL) return -1;
    if (alpha == 0) return 0; /* 完全透明无需操作 */

    /* 使用默认模式如果指定了无效模式 */
    if (mode < FR_COMPOSITE_SRC_OVER || mode > FR_COMPOSITE_LIGHTEN) {
        mode = FR_COMPOSITE_SRC_OVER;
    }

    /* 计算实际绘制区域 (裁剪到目标表面边界) */
    int sx = 0, sy = 0;
    int dx = x, dy = y;
    int sw = src->w, sh = src->h;

    if (dx < 0) { sw += dx; sx -= dx; dx = 0; }
    if (dy < 0) { sh += dy; sy -= dy; dy = 0; }
    if (dx + sw > dst->w) sw = dst->w - dx;
    if (dy + sh > dst->h) sh = dst->h - dy;
    if (sw <= 0 || sh <= 0) return 0;

    uint32_t src_pitch = src->pitch / 4;
    uint32_t dst_pitch = dst->pitch / 4;

    /* 根据混合模式执行像素级合成 */
    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            uint32_t sp = src->buffer[(sy + py) * src_pitch + (sx + px)];
            uint32_t dp = dst->buffer[(dy + py) * dst_pitch + (dx + px)];

            uint8_t sr = (sp >> 16) & 0xFF;
            uint8_t sg = (sp >> 8) & 0xFF;
            uint8_t sb = sp & 0xFF;
            uint8_t dr = (dp >> 16) & 0xFF;
            uint8_t dg = (dp >> 8) & 0xFF;
            uint8_t db = dp & 0xFF;

            uint8_t rr, rg, rb;
            uint16_t inv_alpha = 255 - alpha;

            switch (mode) {
            case FR_COMPOSITE_SRC_OVER:
                rr = (uint8_t)((sr * alpha + dr * inv_alpha) / 255);
                rg = (uint8_t)((sg * alpha + dg * inv_alpha) / 255);
                rb = (uint8_t)((sb * alpha + db * inv_alpha) / 255);
                break;

            case FR_COMPOSITE_SRC_IN:
                rr = (uint8_t)(sr * alpha / 255);
                rg = (uint8_t)(sg * alpha / 255);
                rb = (uint8_t)(sb * alpha / 255);
                break;

            case FR_COMPOSITE_MULTIPLY:
                rr = (uint8_t)(((sr * dr / 255) * alpha +
                               dr * inv_alpha) / 255);
                rg = (uint8_t)(((sg * dg / 255) * alpha +
                               dg * inv_alpha) / 255);
                rb = (uint8_t)(((sb * db / 255) * alpha +
                               db * inv_alpha) / 255);
                break;

            case FR_COMPOSITE_SCREEN: {
                uint16_t scr_r = sr + dr - sr * dr / 255;
                uint16_t scr_g = sg + dg - sg * dg / 255;
                uint16_t scr_b = sb + db - sb * db / 255;
                rr = (uint8_t)((scr_r * alpha + dr * inv_alpha) / 255);
                rg = (uint8_t)((scr_g * alpha + dg * inv_alpha) / 255);
                rb = (uint8_t)((scr_b * alpha + db * inv_alpha) / 255);
                break;
            }

            case FR_COMPOSITE_OVERLAY: {
                uint16_t ov_r, ov_g, ov_b;
                if (dr < 128)
                    ov_r = sr * dr * 2 / 255;
                else
                    ov_r = 510 - (255 - sr) * (255 - dr) * 2 / 255;
                if (dg < 128)
                    ov_g = sg * dg * 2 / 255;
                else
                    ov_g = 510 - (255 - sg) * (255 - dg) * 2 / 255;
                if (db < 128)
                    ov_b = sb * db * 2 / 255;
                else
                    ov_b = 510 - (255 - sb) * (255 - db) * 2 / 255;
                rr = (uint8_t)((ov_r * alpha + dr * inv_alpha) / 255);
                rg = (uint8_t)((ov_g * alpha + dg * inv_alpha) / 255);
                rb = (uint8_t)((ov_b * alpha + db * inv_alpha) / 255);
                break;
            }

            case FR_COMPOSITE_DARKEN:
                rr = (uint8_t)(((sr < dr ? sr : dr) * alpha +
                               dr * inv_alpha) / 255);
                rg = (uint8_t)(((sg < dg ? sg : dg) * alpha +
                               dg * inv_alpha) / 255);
                rb = (uint8_t)(((sb < db ? sb : db) * alpha +
                               db * inv_alpha) / 255);
                break;

            case FR_COMPOSITE_LIGHTEN: {
                uint16_t lr = (sr > dr) ? sr : dr;
                uint16_t lg = (sg > dg) ? sg : dg;
                uint16_t lb = (sb > db) ? sb : db;
                rr = (uint8_t)((lr * alpha + dr * inv_alpha) / 255);
                rg = (uint8_t)((lg * alpha + dg * inv_alpha) / 255);
                rb = (uint8_t)((lb * alpha + db * inv_alpha) / 255);
                break;
            }

            default:
                rr = dr; rg = dg; rb = db;
                break;
            }

            dst->buffer[(dy + py) * dst_pitch + (dx + px)] =
                ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
        }
    }

    return 0;
}

/* ---- 双缓冲支持 ---- */

/*
 * compositor_double_buffer_init - 初始化双缓冲区
 *
 * 创建前后两个相同大小的表面用于双缓冲渲染。
 * 返回 0=成功, -1=内存不足。
 */
int compositor_double_buffer_init(double_buffer_t *db, int w, int h)
{
    if (db == NULL) return -1;
    if (w <= 0 || h <= 0) return -1;

    memset(db, 0, sizeof(double_buffer_t));

    /* 分配前缓冲 */
    db->front = (fr_surface_t *)fr_alloc(sizeof(fr_surface_t));
    if (db->front == NULL) return -1;
    memset(db->front, 0, sizeof(fr_surface_t));

    db->front->buffer = (uint32_t *)fr_alloc((uint32_t)(w * h * 4));
    if (db->front->buffer == NULL) {
        fr_free(db->front);
        db->front = NULL;
        return -1;
    }
    db->front->w = w;
    db->front->h = h;
    db->front->pitch = w * 4;
    db->front->bpp = 32;
    memset(db->front->buffer, 0, (size_t)(w * h * 4));

    /* 分配后缓冲 */
    db->back = (fr_surface_t *)fr_alloc(sizeof(fr_surface_t));
    if (db->back == NULL) {
        fr_free(db->front->buffer);
        fr_free(db->front);
        db->front = NULL;
        return -1;
    }
    memset(db->back, 0, sizeof(fr_surface_t));

    db->back->buffer = (uint32_t *)fr_alloc((uint32_t)(w * h * 4));
    if (db->back->buffer == NULL) {
        fr_free(db->back);
        fr_free(db->front->buffer);
        fr_free(db->front);
        db->front = db->back = NULL;
        return -1;
    }
    db->back->w = w;
    db->back->h = h;
    db->back->pitch = w * 4;
    db->back->bpp = 32;
    memset(db->back->buffer, 0, (size_t)(w * h * 4));

    db->needs_swap = 0;
    return 0;
}

/*
 * compositor_swap_buffers - 交换前后缓冲区
 *
 * 将后缓冲的内容复制到前缓冲，然后交换指针。
 * 返回 0=成功, -1=失败。
 */
int compositor_swap_buffers(double_buffer_t *db)
{
    if (db == NULL || db->front == NULL || db->back == NULL) return -1;

    /* 将后缓冲内容复制到前缓冲 */
    uint32_t size = (uint32_t)(db->front->w * db->front->h);
    memcpy(db->front->buffer, db->back->buffer, size * 4);

    db->needs_swap = 0;
    return 0;
}

/*
 * compositor_get_backbuffer - 获取后缓冲表面指针
 *
 * 返回后缓冲指针，用于渲染到后台。
 */
fr_surface_t *compositor_get_backbuffer(double_buffer_t *db)
{
    if (db == NULL) return NULL;
    db->needs_swap = 1;
    return db->back;
}

/* ================================================================
 *  性能统计
 * ================================================================ */

static comp_stats_t stats;

/*
 * compositor_get_stats - 获取合成器性能统计
 */
comp_stats_t compositor_get_stats(void)
{
    return stats;
}

/*
 * compositor_reset_stats - 重置性能统计计数器
 */
void compositor_reset_stats(void)
{
    memset(&stats, 0, sizeof(comp_stats_t));
}
