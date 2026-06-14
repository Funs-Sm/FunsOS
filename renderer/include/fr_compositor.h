/*
 * fr_compositor.h - 窗口合成器接口定义
 *
 * 提供多窗口/多图层的合成管理功能:
 *   - 多表面 (Surface) 管理与叠加
 *   - Alpha 混合支持
 *   - Z-Order 排序
 *   - 脏区域 (Damage Region) 跟踪
 *   - 硬件加速提示
 */

#ifndef FR_COMPOSITOR_H
#define FR_COMPOSITOR_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;
struct fr_widget;

/* ---- 合成器表面 (Surface) 定义 ---- */

/* 表面类型 */
#define FR_SURFACE_WINDOW       1   /* 普通窗口 */
#define FR_SURFACE_POPUP        2   /* 弹出菜单/对话框 */
#define FR_SURFACE_TOOLTIP      3   /* 工具提示 */
#define FR_SURFACE_CURSOR       4   /* 鼠标光标 */
#define FR_SURFACE_OVERLAY      5   /* 覆盖层 (如视频) */
#define FR_SURFACE_DESKTOP      6   /* 桌面背景 */

/* 合成模式 */
#define FR_COMPOSITE_SRC_OVER   0   /* 标准源覆盖 (Porter-Duff) */
#define FR_COMPOSITE_SRC_IN     1   /* 源在目标内 */
#define FR_COMPOSITE_SRC_OUT    2   /* 源在目标外 */
#define FR_COMPOSITE_ADDITIVE   3   /* 加法混合 (发光效果) */
#define FR_COMPOSITE_MULTIPLY   4   /* 正片叠底 */
#define FR_COMPOSITE_SCREEN     5   /* 滤色 */

/* 表面结构 */
typedef struct fr_surface {
    uint32_t id;                    /* 唯一标识符 */
    uint32_t type;                  /* 表面类型 (FR_SURFACE_*) */
    int x, y;                       /* 屏幕位置 */
    int w, h;                       /* 尺寸 */
    uint8_t alpha;                  /* 全局透明度 (0-255) */
    int z_order;                    /* Z 序值 (越大越靠前) */
    uint32_t composite_mode;        /* 合成模式 */
    uint32_t *buffer;               /* 像素缓冲区指针 */
    uint32_t pitch;                 /* 行字节数 */
    uint32_t bpp;                   /* 位深度 */
    uint32_t flags;                 /* 标志位 */
    void *user_data;                /* 用户数据 */
    struct fr_surface *prev;        /* Z 序前驱 */
    struct fr_surface *next;        /* Z 序后继 */
} fr_surface_t;

/* 表面标志 */
#define FR_SURFACE_VISIBLE         0x01  /* 可见 */
#define FR_SURFACE_OPACITY         0x02  /* 使用不透明度 */
#define FR_SURFACE_HW_ACCEL        0x04  /* 支持硬件加速 */
#define FR_SURFACE_DAMAGE          0x08  /* 有脏区域需要重绘 */
#define FR_SURFACE_CLIP_CHILDREN   0x10  /* 裁剪子区域 */
#define FR_SURFACE_FORCE_OPAQUE    0x20  /* 强制不透明 */

/* ---- 脏区域 (Damage Region) ---- */

/* 最大脏矩形数量 */
#define FR_MAX_DAMAGE_RECTS        64

/* 脏矩形 */
typedef struct {
    int x, y, w, h;
} fr_damage_rect_t;

/* ---- 合成器状态 ---- */

/* 最大同时管理的表面数 */
#define FR_MAX_SURFACES            128

typedef struct fr_compositor {
    fr_surface_t *surfaces;        /* 表面数组 */
    uint32_t surface_count;        /* 当前表面数 */
    uint32_t max_surfaces;         /* 最大容量 */
    fr_surface_t *z_list_head;     /* Z 序链表头 */
    fr_surface_t *z_list_tail;     /* Z 序链表尾 */
    uint32_t next_id;              /* 下一个可用 ID */

    /* 脏区域跟踪 */
    fr_damage_rect_t damage_rects[FR_MAX_DAMAGE_RECTS];
    uint32_t damage_count;
    int damage_fullscreen;         /* 全屏脏标记 */

    /* 目标帧缓冲信息 */
    uint32_t *framebuffer;
    int fb_width, fb_height;
    int fb_pitch;

    /* 统计信息 */
    uint32_t composite_count;
    uint32_t blend_pixel_count;
    uint32_t skip_count;           /* 因无变化而跳过的像素数 */

    /* 硬件加速标志 */
    uint32_t hw_accel_available;
    uint32_t hw_accel_enabled;
} fr_compositor_t;

/* ---- 公共 API ---- */

/* 初始化合成器 */
void fr_compositor_init(fr_compositor_t *comp, uint32_t *fb,
                         int width, int height, int pitch);

/* 销毁合成器，释放所有资源 */
void fr_compositor_shutdown(fr_compositor_t *comp);

/* 创建并添加一个新表面 */
fr_surface_t *fr_compositor_add_surface(fr_compositor_t *comp,
                                          uint32_t type,
                                          int x, int y, int w, int h);

/* 移除指定表面 */
int fr_compositor_remove_surface(fr_compositor_t *comp, uint32_t surface_id);

/* 通过 ID 查找表面 */
fr_surface_t *fr_compositor_find_surface(fr_compositor_t *comp, uint32_t surface_id);

/* 设置表面的 Z 序 */
void fr_compositor_set_zorder(fr_compositor_t *comp,
                               uint32_t surface_id, int z_order);

/* 执行合成: 将所有可见表面按 Z 序混合到帧缓冲 */
void fr_compositor_composite(fr_compositor_t *comp);

/* 添加脏区域 */
void fr_compositor_damage_add(fr_compositor_t *comp,
                               int x, int y, int w, int h);

/* 标记全屏为脏 */
void fr_compositor_damage_all(fr_compositor_t *comp);

/* 刷新脏区域: 只重绘脏区域内的内容 */
void fr_compositor_damage_flush(fr_compositor_t *comp);

/* 清除所有脏区域标记 */
void fr_compositor_damage_clear(fr_compositor_t *comp);

/* 设置表面位置 */
void fr_compositor_set_surface_pos(fr_compositor_t *comp,
                                    uint32_t surface_id, int x, int y);

/* 设置表面尺寸 */
void fr_compositor_set_surface_size(fr_compositor_t *comp,
                                     uint32_t surface_id, int w, int h);

/* 设置表面透明度 */
void fr_compositor_set_surface_alpha(fr_compositor_t *comp,
                                      uint32_t surface_id, uint8_t alpha);

/* 设置表面可见性 */
void fr_compositor_set_surface_visible(fr_compositor_t *comp,
                                        uint32_t surface_id, int visible);

/* 设置表面缓冲区 */
void fr_compositor_set_surface_buffer(fr_compositor_t *comp,
                                       uint32_t surface_id,
                                       uint32_t *buffer, uint32_t pitch);

/* 获取脏区域统计 */
uint32_t fr_compositor_get_damage_count(fr_compositor_t *comp);
int fr_compositor_is_fullscreen_dirty(fr_compositor_t *comp);

/* 启用/禁用硬件加速提示 */
void fr_compositor_set_hw_accel(fr_compositor_t *comp, int enabled);

#endif /* FR_COMPOSITOR_H */
