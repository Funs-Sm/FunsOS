/* clipboard.c - 剪贴板集成实现
 * 实现文本复制/剪切/粘贴、多格式数据、
 * 剪贴板历史 (最近 10 条) 和变更通知回调
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_clipboard.h"
#include "string.h"

/* 内部使用的系统时间戳 (毫秒, 从系统启动算起) */
static uint32_t get_timestamp(void)
{
    /* 
     * 在实际内核中, 这应该从 PIT/HPET/APIC 定时器获取。
     * 简化实现: 返回一个静态计数器。
     */
    static uint32_t mock_timestamp = 0;
    mock_timestamp += 1;
    return mock_timestamp;
}

/* ================================================================
 *  剪贴板生命周期
 * ================================================================ */

/*
 * fr_clipboard_create - 创建剪贴板管理器
 */
fr_clipboard_t *fr_clipboard_create(void)
{
    fr_clipboard_t *cb = (fr_clipboard_t *)fr_alloc(
        (uint32_t)sizeof(fr_clipboard_t));
    if (cb == NULL) return NULL;

    memset(cb, 0, sizeof(fr_clipboard_t));
    return cb;
}

/*
 * fr_clipboard_destroy - 销毁剪贴板管理器
 */
void fr_clipboard_destroy(fr_clipboard_t *cb)
{
    if (cb == NULL) return;

    /* 释放条目数据 */
    for (int i = 0; i < 4; i++) {
        if (cb->entries[i].data != NULL) {
            fr_free(cb->entries[i].data);
        }
    }

    /* 释放文本缓存 */
    if (cb->text != NULL) {
        fr_free(cb->text);
    }

    /* 释放历史条目的文本 */
    for (int i = 0; i < FR_CLIPBOARD_HISTORY_MAX; i++) {
        if (cb->history[i].text != NULL) {
            fr_free(cb->history[i].text);
        }
    }

    fr_free(cb);
}

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/* 获取指定格式的条目索引 */
static int get_entry_index(uint32_t format)
{
    switch (format) {
    case FR_CLIPBOARD_FORMAT_TEXT:       return 0;
    case FR_CLIPBOARD_FORMAT_RICH_TEXT:  return 1;
    case FR_CLIPBOARD_FORMAT_IMAGE:      return 2;
    case FR_CLIPBOARD_FORMAT_FILE_LIST:  return 3;
    default:                             return -1;
    }
}

/* 设置指定格式的条目 */
static int set_entry(fr_clipboard_t *cb, uint32_t format,
                     const void *data, uint32_t size)
{
    if (cb == NULL || data == NULL || size == 0) return -1;
    if (size > FR_CLIPBOARD_MAX_DATA_SIZE) return -1;

    int idx = get_entry_index(format);
    if (idx < 0) return -1;

    /* 释放旧数据 */
    if (cb->entries[idx].data != NULL) {
        fr_free(cb->entries[idx].data);
    }

    /* 分配新缓冲区并拷贝 */
    uint8_t *new_data = (uint8_t *)fr_alloc(size);
    if (new_data == NULL) return -1;

    memcpy(new_data, data, size);

    cb->entries[idx].format = format;
    cb->entries[idx].size = size;
    cb->entries[idx].data = new_data;
    cb->entries[idx].timestamp = get_timestamp();

    /* 设置格式位掩码 */
    cb->format_mask |= (1u << idx);

    cb->owned = 1;
    cb->sequence++;

    return 0;
}

/* 获取指定格式的条目 */
static const fr_clipboard_entry_t *get_entry(fr_clipboard_t *cb, uint32_t format)
{
    if (cb == NULL || !cb->owned) return NULL;

    int idx = get_entry_index(format);
    if (idx < 0) return NULL;

    if (!(cb->format_mask & (1u << idx))) return NULL;

    return &cb->entries[idx];
}

/* 从格式掩码获取格式类型 */
static uint32_t idx_to_format(int idx)
{
    switch (idx) {
    case 0: return FR_CLIPBOARD_FORMAT_TEXT;
    case 1: return FR_CLIPBOARD_FORMAT_RICH_TEXT;
    case 2: return FR_CLIPBOARD_FORMAT_IMAGE;
    case 3: return FR_CLIPBOARD_FORMAT_FILE_LIST;
    default: return 0xFFFFFFFF;
    }
}

/* 添加文本到历史记录 */
static void add_to_history(fr_clipboard_t *cb, const char *text, uint32_t len)
{
    if (cb == NULL || text == NULL || len == 0) return;

    /* 复制文本 */
    char *copy = (char *)fr_alloc(len + 1);
    if (copy == NULL) return;

    memcpy(copy, text, len);
    copy[len] = '\0';

    /* 如果头部位置已有旧条目, 释放它 */
    if (cb->history[cb->history_head].text != NULL) {
        fr_free(cb->history[cb->history_head].text);
    }

    cb->history[cb->history_head].text = copy;
    cb->history[cb->history_head].text_len = len;
    cb->history[cb->history_head].timestamp = get_timestamp();

    /* 环形缓冲推进 */
    cb->history_head = (cb->history_head + 1) % FR_CLIPBOARD_HISTORY_MAX;
    if (cb->history_count < FR_CLIPBOARD_HISTORY_MAX) {
        cb->history_count++;
    }
}

/* 触发变更通知 */
static void notify_change(fr_clipboard_t *cb, int event_type, uint32_t format)
{
    if (cb == NULL) return;
    if (cb->on_change != NULL) {
        cb->on_change(event_type, format, cb->notify_ctx);
    }
}

/* ================================================================
 *  文本操作
 * ================================================================ */

/*
 * fr_clipboard_copy_text - 复制文本到剪贴板
 *
 * 将文本复制到剪贴板, 同时保存到历史记录和触发通知。
 * 返回 0=成功, -1=失败。
 */
int fr_clipboard_copy_text(fr_clipboard_t *cb, const char *text, uint32_t len)
{
    if (cb == NULL || text == NULL || len == 0) return -1;

    /* 将纯文本作为数据条目保存 */
    int result = set_entry(cb, FR_CLIPBOARD_FORMAT_TEXT,
                           (const void *)text, len);
    if (result != 0) return result;

    /* 更新文本快速访问缓存 */
    if (cb->text != NULL) {
        fr_free(cb->text);
    }
    cb->text = (char *)fr_alloc(len + 1);
    if (cb->text != NULL) {
        memcpy(cb->text, text, len);
        cb->text[len] = '\0';
        cb->text_len = len;
    }

    /* 添加到历史 */
    add_to_history(cb, text, len);

    /* 触发通知 */
    notify_change(cb, FR_CLIPBOARD_EVENT_COPY, FR_CLIPBOARD_FORMAT_TEXT);

    return 0;
}

/*
 * fr_clipboard_cut_text - 剪切文本
 *
 * 与复制相同, 但通知事件标记为剪切(CUT)。
 */
int fr_clipboard_cut_text(fr_clipboard_t *cb, const char *text, uint32_t len)
{
    if (cb == NULL || text == NULL || len == 0) return -1;

    int result = fr_clipboard_copy_text(cb, text, len);
    if (result == 0) {
        notify_change(cb, FR_CLIPBOARD_EVENT_CUT, FR_CLIPBOARD_FORMAT_TEXT);
    }
    return result;
}

/*
 * fr_clipboard_paste_text - 从剪贴板粘贴文本
 *
 * 返回新分配的内存缓冲区, 包含剪贴板中的文本。
 * 调用者负责使用 fr_free() 释放返回值。
 * out_len 如果不为 NULL, 则设置为文本长度。
 */
char *fr_clipboard_paste_text(fr_clipboard_t *cb, uint32_t *out_len)
{
    if (cb == NULL || !cb->owned) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    const fr_clipboard_entry_t *entry = get_entry(cb,
        FR_CLIPBOARD_FORMAT_TEXT);
    if (entry == NULL || entry->data == NULL) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    /* 分配新缓冲区 */
    uint32_t len = entry->size;
    char *result = (char *)fr_alloc(len + 1);
    if (result == NULL) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    memcpy(result, entry->data, len);
    result[len] = '\0';

    if (out_len != NULL) *out_len = len;

    /* 触发粘贴通知 */
    notify_change(cb, FR_CLIPBOARD_EVENT_PASTE, FR_CLIPBOARD_FORMAT_TEXT);

    return result;
}

/*
 * fr_clipboard_has_text - 检查剪贴板是否包含文本
 */
int fr_clipboard_has_text(fr_clipboard_t *cb)
{
    return fr_clipboard_has_format(cb, FR_CLIPBOARD_FORMAT_TEXT);
}

/* ================================================================
 *  多格式操作
 * ================================================================ */

/*
 * fr_clipboard_set_data - 复制指定格式的数据到剪贴板
 */
int fr_clipboard_set_data(fr_clipboard_t *cb, uint32_t format,
                           const void *data, uint32_t size)
{
    if (cb == NULL || data == NULL || size == 0) return -1;

    int result = set_entry(cb, format, data, size);
    if (result == 0 && format == FR_CLIPBOARD_FORMAT_TEXT) {
        /* 同步文本缓存 */
        if (cb->text != NULL) {
            fr_free(cb->text);
        }
        cb->text = (char *)fr_alloc(size + 1);
        if (cb->text != NULL) {
            memcpy(cb->text, data, size);
            cb->text[size] = '\0';
            cb->text_len = size;
        }
        add_to_history(cb, (const char *)data, size);
    }

    if (result == 0) {
        notify_change(cb, FR_CLIPBOARD_EVENT_COPY, format);
    }

    return result;
}

/*
 * fr_clipboard_get_data - 从剪贴板获取指定格式的数据
 *
 * inout_size: 输入时为 out_buffer 的容量, 输出时为实际数据大小。
 * 如果 out_buffer 容量不足, 返回 -2。
 * 返回 0=成功, -1=无数据, -2=缓冲区不足。
 */
int fr_clipboard_get_data(fr_clipboard_t *cb, uint32_t format,
                          void *out_buffer, uint32_t *inout_size)
{
    if (cb == NULL || out_buffer == NULL || inout_size == NULL) return -1;

    const fr_clipboard_entry_t *entry = get_entry(cb, format);
    if (entry == NULL || entry->data == NULL) return -1;

    if (*inout_size < entry->size) {
        *inout_size = entry->size;
        return -2; /* 缓冲区不足 */
    }

    memcpy(out_buffer, entry->data, entry->size);
    *inout_size = entry->size;

    notify_change(cb, FR_CLIPBOARD_EVENT_PASTE, format);

    return 0;
}

/*
 * fr_clipboard_has_format - 检查剪贴板是否包含指定格式
 */
int fr_clipboard_has_format(fr_clipboard_t *cb, uint32_t format)
{
    if (cb == NULL || !cb->owned) return 0;

    int idx = get_entry_index(format);
    if (idx < 0) return 0;

    return (cb->format_mask & (1u << idx)) ? 1 : 0;
}

/*
 * fr_clipboard_get_formats - 获取当前所有存在的格式 (位掩码)
 *
 * 返回: 位掩码, 每个位对应一个格式条目。
 * 可以用 FR_CLIPBOARD_FORMAT_* 对应的 idx 来检查。
 */
uint32_t fr_clipboard_get_formats(fr_clipboard_t *cb)
{
    if (cb == NULL) return 0;
    return cb->format_mask;
}

/* ================================================================
 *  图像引用
 * ================================================================ */

/*
 * fr_clipboard_set_image - 复制图像引用到剪贴板
 */
int fr_clipboard_set_image(fr_clipboard_t *cb,
                           const uint32_t *pixels,
                           int width, int height, int bpp)
{
    if (cb == NULL || pixels == NULL) return -1;
    if (width <= 0 || height <= 0 || bpp <= 0) return -1;

    /* 计算图像数据大小 */
    uint32_t img_size = (uint32_t)(width * height * bpp / 8);
    if (img_size == 0 || img_size > FR_CLIPBOARD_MAX_DATA_SIZE) return -1;

    /* 构建图像数据: [元数据头] + [像素数据] */
    fr_clipboard_image_info_t info;
    info.width = width;
    info.height = height;
    info.bpp = bpp;

    uint32_t total_size = (uint32_t)sizeof(fr_clipboard_image_info_t) + img_size;

    uint8_t *buffer = (uint8_t *)fr_alloc(total_size);
    if (buffer == NULL) return -1;

    /* 先写元数据头 */
    memcpy(buffer, &info, sizeof(fr_clipboard_image_info_t));

    /* 再写像素数据 */
    memcpy(buffer + sizeof(fr_clipboard_image_info_t), pixels, img_size);

    int result = set_entry(cb, FR_CLIPBOARD_FORMAT_IMAGE,
                           buffer, total_size);

    fr_free(buffer);
    return result;
}

/*
 * fr_clipboard_get_image - 从剪贴板获取图像引用
 *
 * 返回 0=成功, -1=无图像数据, -2=缓冲区不足。
 */
int fr_clipboard_get_image(fr_clipboard_t *cb,
                           uint32_t *out_pixels, int max_pixels,
                           int *out_width, int *out_height, int *out_bpp)
{
    if (cb == NULL || out_pixels == NULL) return -1;

    const fr_clipboard_entry_t *entry = get_entry(cb,
        FR_CLIPBOARD_FORMAT_IMAGE);
    if (entry == NULL || entry->data == NULL) return -1;

    /* 读取元数据头 */
    if (entry->size < sizeof(fr_clipboard_image_info_t)) return -1;

    fr_clipboard_image_info_t info;
    memcpy(&info, entry->data, sizeof(fr_clipboard_image_info_t));

    uint32_t img_size = entry->size - (uint32_t)sizeof(fr_clipboard_image_info_t);
    int pixel_count = (int)(img_size * 8 / (uint32_t)info.bpp);

    if (pixel_count > max_pixels) {
        return -2; /* 缓冲区不足 */
    }

    /* 拷贝像素数据 */
    memcpy(out_pixels,
           entry->data + sizeof(fr_clipboard_image_info_t),
           img_size);

    if (out_width != NULL)  *out_width = info.width;
    if (out_height != NULL) *out_height = info.height;
    if (out_bpp != NULL)    *out_bpp = info.bpp;

    return 0;
}

/* ================================================================
 *  剪贴板历史
 * ================================================================ */

/*
 * fr_clipboard_history_count - 获取历史条目数量
 */
int fr_clipboard_history_count(fr_clipboard_t *cb)
{
    if (cb == NULL) return 0;
    return cb->history_count;
}

/*
 * fr_clipboard_history_get - 获取指定索引的历史条目
 *
 * 索引 0 = 最新的条目。
 * 返回文本字符串 (内部指针, 不要释放)。
 * out_len 如果不为 NULL, 则设置为文本长度。
 */
const char *fr_clipboard_history_get(fr_clipboard_t *cb, int index,
                                      uint32_t *out_len)
{
    if (cb == NULL || index < 0 || index >= cb->history_count) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    /* 计算实际存储索引 (环形缓冲) */
    int actual_idx = (cb->history_head - 1 - index + FR_CLIPBOARD_HISTORY_MAX)
                     % FR_CLIPBOARD_HISTORY_MAX;

    if (cb->history[actual_idx].text == NULL) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    if (out_len != NULL) {
        *out_len = cb->history[actual_idx].text_len;
    }

    return cb->history[actual_idx].text;
}

/*
 * fr_clipboard_history_clear - 清空所有历史
 */
void fr_clipboard_history_clear(fr_clipboard_t *cb)
{
    if (cb == NULL) return;

    for (int i = 0; i < FR_CLIPBOARD_HISTORY_MAX; i++) {
        if (cb->history[i].text != NULL) {
            fr_free(cb->history[i].text);
            cb->history[i].text = NULL;
            cb->history[i].text_len = 0;
            cb->history[i].timestamp = 0;
        }
    }

    cb->history_head = 0;
    cb->history_count = 0;
}

/* ================================================================
 *  清空
 * ================================================================ */

/*
 * fr_clipboard_clear - 清空剪贴板所有内容
 */
void fr_clipboard_clear(fr_clipboard_t *cb)
{
    if (cb == NULL) return;

    /* 清空所有条目数据 */
    for (int i = 0; i < 4; i++) {
        if (cb->entries[i].data != NULL) {
            fr_free(cb->entries[i].data);
            cb->entries[i].data = NULL;
            cb->entries[i].size = 0;
        }
    }

    cb->format_mask = 0;

    /* 清空文本缓存 */
    if (cb->text != NULL) {
        fr_free(cb->text);
        cb->text = NULL;
        cb->text_len = 0;
    }

    cb->owned = 0;

    /* 序列号递增表示变更 */
    cb->sequence++;

    /* 触发清空通知 */
    notify_change(cb, FR_CLIPBOARD_EVENT_CLEAR, 0);
}

/* ================================================================
 *  通知回调
 * ================================================================ */

/*
 * fr_clipboard_set_notify - 设置剪贴板变更通知回调
 */
void fr_clipboard_set_notify(fr_clipboard_t *cb,
                              fr_clipboard_notify_fn callback,
                              fr_clipboard_callback_ctx_t ctx)
{
    if (cb == NULL) return;
    cb->on_change = callback;
    cb->notify_ctx = ctx;
}

/* ================================================================
 *  状态查询
 * ================================================================ */

/*
 * fr_clipboard_is_owned - 获取剪贴板所有权状态
 */
int fr_clipboard_is_owned(fr_clipboard_t *cb)
{
    if (cb == NULL) return 0;
    return cb->owned;
}

/*
 * fr_clipboard_get_sequence - 获取当前剪贴板内容序列号
 *
 * 每次内容变更时递增, 可用于检测是否需要重新获取数据。
 */
uint32_t fr_clipboard_get_sequence(fr_clipboard_t *cb)
{
    if (cb == NULL) return 0;
    return cb->sequence;
}