/* fr_clipboard.h - 剪贴板集成
 * 提供文本复制/剪切/粘贴、多种数据格式、
 * 剪贴板历史和变更通知功能
 */

#ifndef FR_CLIPBOARD_H
#define FR_CLIPBOARD_H

#include "stdint.h"

/* ---- 剪贴板格式 ---- */

/* 格式类型 */
#define FR_CLIPBOARD_FORMAT_TEXT        0   /* 纯文本 */
#define FR_CLIPBOARD_FORMAT_RICH_TEXT   1   /* 富文本 (带样式) */
#define FR_CLIPBOARD_FORMAT_IMAGE       2   /* 图像 (像素缓冲区引用) */
#define FR_CLIPBOARD_FORMAT_FILE_LIST   3   /* 文件列表路径 */

/* 每种格式数据最大字节数 */
#define FR_CLIPBOARD_MAX_DATA_SIZE  1048576  /* 1 MB */

/* ---- 剪贴板条目 ---- */

/* 单条剪贴板数据 */
typedef struct {
    uint32_t format;                    /* 数据格式 */
    uint32_t size;                      /* 数据大小 (字节) */
    uint8_t *data;                      /* 数据指针 */
    uint32_t timestamp;                 /* 复制时的系统时间戳 */
} fr_clipboard_entry_t;

/* 图像引用的元数据 (用于 FR_CLIPBOARD_FORMAT_IMAGE) */
typedef struct {
    int width;
    int height;
    int bpp;
    uint16_t palette[256];             /* 调色板 (如果支持) */
} fr_clipboard_image_info_t;

/* ---- 剪贴板通知回调 ---- */

/* 回调上下文 */
typedef void *fr_clipboard_callback_ctx_t;

/* 剪贴板变更事件类型 */
#define FR_CLIPBOARD_EVENT_COPY    0   /* 数据被复制 */
#define FR_CLIPBOARD_EVENT_CUT     1   /* 数据被剪切 */
#define FR_CLIPBOARD_EVENT_PASTE   2   /* 数据被粘贴 */
#define FR_CLIPBOARD_EVENT_CLEAR   3   /* 剪贴板被清空 */

/* 变更通知回调 */
typedef void (*fr_clipboard_notify_fn)(int event_type,
                                        uint32_t format,
                                        fr_clipboard_callback_ctx_t ctx);

/* ---- 剪贴板历史 ---- */

/* 历史最多条目数 */
#define FR_CLIPBOARD_HISTORY_MAX   10

/* 历史条目 */
typedef struct {
    char *text;             /* 文本内容 (NULL 表示非文本) */
    uint32_t text_len;      /* 文本长度 */
    uint32_t timestamp;     /* 时间戳 */
} fr_clipboard_history_t;

/* ---- 剪贴板管理器 ---- */

typedef struct fr_clipboard {
    /* 当前剪贴板内容 */
    fr_clipboard_entry_t entries[4];     /* 每种格式最多一个条目 */
    uint32_t format_mask;                /* 存在哪些格式的位掩码 */

    /* 文本内容快速访问 (冗余, 用于加速文本操作) */
    char *text;
    uint32_t text_len;

    /* 剪贴板历史 */
    fr_clipboard_history_t history[FR_CLIPBOARD_HISTORY_MAX];
    int history_head;                   /* 环形缓冲区写入位置 */
    int history_count;                  /* 历史条目数 (最多 10) */

    /* 变更通知 */
    fr_clipboard_notify_fn on_change;
    fr_clipboard_callback_ctx_t notify_ctx;

    /* 所有权跟踪 */
    int owned;                          /* 是否有有效的剪贴板数据 */
    uint32_t owner_id;                  /* 所有权标识 */
    uint32_t sequence;                  /* 序列号, 每次内容变更递增 */
} fr_clipboard_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* ---- 生命周期 ---- */

/* 创建剪贴板管理器 */
fr_clipboard_t *fr_clipboard_create(void);

/* 销毁剪贴板管理器 */
void fr_clipboard_destroy(fr_clipboard_t *cb);

/* ---- 文本操作 ---- */

/* 复制文本到剪贴板 */
int fr_clipboard_copy_text(fr_clipboard_t *cb, const char *text, uint32_t len);

/* 剪切文本 (复制并标记来源) */
int fr_clipboard_cut_text(fr_clipboard_t *cb, const char *text, uint32_t len);

/* 从剪贴板粘贴文本 (返回新分配的内存, 调用者负责释放) */
char *fr_clipboard_paste_text(fr_clipboard_t *cb, uint32_t *out_len);

/* 检查剪贴板是否包含文本 */
int fr_clipboard_has_text(fr_clipboard_t *cb);

/* ---- 多格式操作 ---- */

/* 复制指定格式的数据到剪贴板 */
int fr_clipboard_set_data(fr_clipboard_t *cb, uint32_t format,
                           const void *data, uint32_t size);

/* 从剪贴板获取指定格式的数据 */
int fr_clipboard_get_data(fr_clipboard_t *cb, uint32_t format,
                          void *out_buffer, uint32_t *inout_size);

/* 检查剪贴板是否包含指定格式 */
int fr_clipboard_has_format(fr_clipboard_t *cb, uint32_t format);

/* 获取当前剪贴板中所有存在的格式 (位掩码) */
uint32_t fr_clipboard_get_formats(fr_clipboard_t *cb);

/* ---- 图像引用 ---- */

/* 复制图像引用到剪贴板 */
int fr_clipboard_set_image(fr_clipboard_t *cb,
                           const uint32_t *pixels,
                           int width, int height, int bpp);

/* 从剪贴板获取图像引用 */
int fr_clipboard_get_image(fr_clipboard_t *cb,
                           uint32_t *out_pixels, int max_pixels,
                           int *out_width, int *out_height, int *out_bpp);

/* ---- 剪贴板历史 ---- */

/* 获取历史条目数量 */
int fr_clipboard_history_count(fr_clipboard_t *cb);

/* 获取指定索引的历史条目 */
const char *fr_clipboard_history_get(fr_clipboard_t *cb, int index,
                                      uint32_t *out_len);

/* 清空所有历史 */
void fr_clipboard_history_clear(fr_clipboard_t *cb);

/* ---- 清空 ---- */

/* 清空剪贴板所有内容 */
void fr_clipboard_clear(fr_clipboard_t *cb);

/* ---- 通知回调 ---- */

/* 设置剪贴板变更通知回调 */
void fr_clipboard_set_notify(fr_clipboard_t *cb,
                              fr_clipboard_notify_fn callback,
                              fr_clipboard_callback_ctx_t ctx);

/* ---- 状态查询 ---- */

/* 获取剪贴板所有权状态 */
int fr_clipboard_is_owned(fr_clipboard_t *cb);

/* 获取当前剪贴板内容序列号 */
uint32_t fr_clipboard_get_sequence(fr_clipboard_t *cb);

#endif /* FR_CLIPBOARD_H */