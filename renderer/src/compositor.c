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
