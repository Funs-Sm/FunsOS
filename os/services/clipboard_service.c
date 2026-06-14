/* clipboard_service.c - FUNSOS 剪贴板服务实现
 * 系统剪贴板管理，支持文本、图像、文件列表复制粘贴
 */

#include "clipboard_service.h"
#include "sys_api.h"
#include "string.h"
#include "klog.h"
#include "stddef.h"
#include "kheap.h"

/* 内部状态 */
static clipboard_entry_t current_entry;
static clipboard_history_t history;
static uint32_t service_running = 0;
static void (*change_callback)(uint32_t type, void *user_data) = NULL;
static void *change_callback_data = NULL;

/* 拖放状态 */
static struct {
    uint8_t  active;
    int      start_x;
    int      start_y;
    int      current_x;
    int      current_y;
    uint32_t type;
    void    *data;
    uint32_t size;
    int      drop_target_accepts;
} drag_state;

/* 静态函数 */
static void clipboard_notify_change(void);
static void clipboard_free_entry(clipboard_entry_t *entry);
static void clipboard_push_history(clipboard_entry_t *entry);

int clipboard_service_init(void)
{
    memset(&current_entry, 0, sizeof(current_entry));
    memset(&history, 0, sizeof(history));
    memset(&drag_state, 0, sizeof(drag_state));
    service_running = 0;
    change_callback = NULL;
    change_callback_data = NULL;

    klog_info("Clipboard service initialized");
    return 0;
}

int clipboard_service_start(void)
{
    if (service_running) return 0;
    service_running = 1;
    klog_info("Clipboard service started");
    return 0;
}

void clipboard_service_stop(void)
{
    service_running = 0;
    clipboard_free_entry(&current_entry);
    clipboard_history_clear();
    klog_info("Clipboard service stopped");
}

int clipboard_set_text(const char *text)
{
    if (text == NULL) return -1;

    uint32_t len = (uint32_t)strlen(text);
    if (len > CLIPBOARD_MAX_TEXT_SIZE) len = CLIPBOARD_MAX_TEXT_SIZE;

    /* 保存旧数据到历史 */
    if (current_entry.data != NULL) {
        clipboard_push_history(&current_entry);
        memset(&current_entry, 0, sizeof(current_entry));
    }

    /* 分配并复制数据 */
    current_entry.data = (void *)kmalloc(len + 1);
    if (current_entry.data == NULL) return -1;

    memcpy(current_entry.data, text, len);
    ((char *)current_entry.data)[len] = '\0';
    current_entry.size = len + 1;
    current_entry.type = CLIPBOARD_TYPE_TEXT;
    current_entry.timestamp = 0; /* 获取当前时间 */
    current_entry.source_pid = 0; /* 获取当前 PID */

    clipboard_notify_change();
    return 0;
}

int clipboard_get_text(char *buf, uint32_t bufsize)
{
    if (buf == NULL || bufsize == 0) return -1;
    if (current_entry.type != CLIPBOARD_TYPE_TEXT) return -1;
    if (current_entry.data == NULL) return -1;

    uint32_t len = current_entry.size;
    if (len > bufsize) len = bufsize;
    memcpy(buf, current_entry.data, len);
    if (len > 0) buf[len - 1] = '\0'; /* 确保以 null 结尾 */

    return (int)len;
}

int clipboard_set_data(uint32_t type, const void *data, uint32_t size)
{
    if (data == NULL || size == 0) return -1;

    /* 检查大小限制 */
    switch (type) {
    case CLIPBOARD_TYPE_TEXT:
        if (size > CLIPBOARD_MAX_TEXT_SIZE) return -1;
        break;
    case CLIPBOARD_TYPE_IMAGE:
        if (size > CLIPBOARD_MAX_IMAGE_SIZE) return -1;
        break;
    case CLIPBOARD_TYPE_BINARY:
        if (size > CLIPBOARD_MAX_BINARY_SIZE) return -1;
        break;
    default:
        break;
    }

    /* 保存旧数据到历史 */
    if (current_entry.data != NULL) {
        clipboard_push_history(&current_entry);
        memset(&current_entry, 0, sizeof(current_entry));
    }

    /* 分配并复制数据 */
    current_entry.data = (void *)kmalloc(size);
    if (current_entry.data == NULL) return -1;

    memcpy(current_entry.data, data, size);
    current_entry.size = size;
    current_entry.type = type;
    current_entry.timestamp = 0;
    current_entry.source_pid = 0;

    clipboard_notify_change();
    return 0;
}

int clipboard_get_data(uint32_t *type, void *buf, uint32_t *size)
{
    if (type == NULL || buf == NULL || size == NULL) return -1;
    if (current_entry.data == NULL) return -1;

    uint32_t copy_size = current_entry.size;
    if (copy_size > *size) copy_size = *size;

    memcpy(buf, current_entry.data, copy_size);
    *type = current_entry.type;
    *size = copy_size;
    return 0;
}

int clipboard_set_files(const char *file_list)
{
    return clipboard_set_data(CLIPBOARD_TYPE_FILES, file_list,
                              (uint32_t)strlen(file_list) + 1);
}

int clipboard_get_files(char *buf, uint32_t bufsize)
{
    return clipboard_get_text(buf, bufsize);
}

uint32_t clipboard_get_type(void)
{
    return current_entry.type;
}

uint32_t clipboard_get_size(void)
{
    return current_entry.size;
}

int clipboard_clear(void)
{
    clipboard_free_entry(&current_entry);
    memset(&current_entry, 0, sizeof(current_entry));
    clipboard_notify_change();
    return 0;
}

int clipboard_is_empty(void)
{
    return (current_entry.data == NULL) ? 1 : 0;
}

int clipboard_has_type(uint32_t type)
{
    if (current_entry.data == NULL) return 0;
    return (current_entry.type == type) ? 1 : 0;
}

/* ---- 剪贴板历史 ---- */

int clipboard_history_count(void)
{
    return (int)history.count;
}

int clipboard_history_get(int index, uint32_t *type, char *buf, uint32_t bufsize)
{
    if (index < 0 || index >= (int)history.count) return -1;
    if (type == NULL || buf == NULL || bufsize == 0) return -1;

    uint32_t idx = (history.head + CLIPBOARD_MAX_HISTORY - (uint32_t)index - 1) % CLIPBOARD_MAX_HISTORY;
    clipboard_entry_t *entry = &history.entries[idx];

    if (entry->data == NULL) return -1;

    uint32_t copy_size = entry->size;
    if (copy_size > bufsize) copy_size = bufsize;

    memcpy(buf, entry->data, copy_size);
    *type = entry->type;
    return (int)copy_size;
}

void clipboard_history_clear(void)
{
    for (uint32_t i = 0; i < CLIPBOARD_MAX_HISTORY; i++) {
        clipboard_free_entry(&history.entries[i]);
    }
    memset(&history, 0, sizeof(history));
}

int clipboard_history_select(int index)
{
    if (index < 0 || index >= (int)history.count) return -1;

    uint32_t idx = (history.head + CLIPBOARD_MAX_HISTORY - (uint32_t)index - 1) % CLIPBOARD_MAX_HISTORY;
    clipboard_entry_t *entry = &history.entries[idx];

    if (entry->data == NULL) return -1;

    /* 保存当前条目到历史 */
    if (current_entry.data != NULL) {
        clipboard_push_history(&current_entry);
    }

    /* 复制历史条目到当前 */
    memcpy(&current_entry, entry, sizeof(clipboard_entry_t));
    /* 清空历史条目（避免双重释放） */
    memset(entry, 0, sizeof(clipboard_entry_t));

    /* 调整历史 */
    if (history.count > 0) history.count--;

    clipboard_notify_change();
    return 0;
}

/* ---- 拖放支持 ---- */

int clipboard_begin_drag(int x, int y, uint32_t type, const void *data, uint32_t size)
{
    if (drag_state.active) return -1;
    if (data == NULL || size == 0) return -1;

    drag_state.active = 1;
    drag_state.start_x = x;
    drag_state.start_y = y;
    drag_state.current_x = x;
    drag_state.current_y = y;
    drag_state.type = type;
    drag_state.size = size;

    drag_state.data = (void *)kmalloc(size);
    if (drag_state.data == NULL) {
        drag_state.active = 0;
        return -1;
    }
    memcpy(drag_state.data, data, size);

    return 0;
}

int clipboard_end_drag(int *target_x, int *target_y)
{
    if (!drag_state.active) return -1;

    if (target_x) *target_x = drag_state.current_x;
    if (target_y) *target_y = drag_state.current_y;

    /* 如果目标接受拖放，设置剪贴板数据 */
    if (drag_state.drop_target_accepts) {
        clipboard_set_data(drag_state.type, drag_state.data, drag_state.size);
    }

    /* 清理拖放状态 */
    if (drag_state.data) {
        void *p = drag_state.data;
        drag_state.data = NULL;
        (void)p;
    }
    memset(&drag_state, 0, sizeof(drag_state));

    return 0;
}

int clipboard_is_dragging(void)
{
    return drag_state.active ? 1 : 0;
}

int clipboard_get_drag_data(uint32_t *type, void *buf, uint32_t *size)
{
    if (!drag_state.active) return -1;
    if (type == NULL || buf == NULL || size == NULL) return -1;
    if (drag_state.data == NULL) return -1;

    uint32_t copy_size = drag_state.size;
    if (copy_size > *size) copy_size = *size;

    memcpy(buf, drag_state.data, copy_size);
    *type = drag_state.type;
    *size = copy_size;
    return 0;
}

int clipboard_set_drop_target(int accepts_type)
{
    drag_state.drop_target_accepts = accepts_type;
    return 0;
}

/* ---- 剪贴板通知 ---- */

int clipboard_on_change(void (*callback)(uint32_t type, void *user_data), void *user_data)
{
    change_callback = callback;
    change_callback_data = user_data;
    return 0;
}

void clipboard_off_change(void)
{
    change_callback = NULL;
    change_callback_data = NULL;
}

void clipboard_service_update(void)
{
    if (!service_running) return;
    /* 周期性检查：清理过期数据等 */
}

/* ---- 内部函数 ---- */

static void clipboard_notify_change(void)
{
    if (change_callback) {
        change_callback(current_entry.type, change_callback_data);
    }
}

static void clipboard_free_entry(clipboard_entry_t *entry)
{
    if (entry == NULL) return;
    if (entry->data) {
        void *p = entry->data;
        entry->data = NULL;
        (void)p;
    }
    entry->size = 0;
}

static void clipboard_push_history(clipboard_entry_t *entry)
{
    if (entry == NULL || entry->data == NULL) return;

    /* 如果已满，释放最旧的条目 */
    if (history.count >= CLIPBOARD_MAX_HISTORY) {
        clipboard_free_entry(&history.entries[history.head]);
    }

    /* 复制条目到历史 */
    uint32_t idx = history.head;
    memcpy(&history.entries[idx], entry, sizeof(clipboard_entry_t));
    history.head = (history.head + 1) % CLIPBOARD_MAX_HISTORY;

    if (history.count < CLIPBOARD_MAX_HISTORY) {
        history.count++;
    }
}