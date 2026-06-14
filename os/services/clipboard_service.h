/* clipboard_service.h - FUNSOS 剪贴板服务
 * 系统剪贴板管理，支持文本、图像、文件列表复制粘贴
 */

#ifndef CLIPBOARD_SERVICE_H
#define CLIPBOARD_SERVICE_H

#include "stdint.h"

/* 剪贴板数据类型 */
#define CLIPBOARD_TYPE_TEXT    0
#define CLIPBOARD_TYPE_RTF     1
#define CLIPBOARD_TYPE_HTML    2
#define CLIPBOARD_TYPE_IMAGE   3
#define CLIPBOARD_TYPE_FILES   4
#define CLIPBOARD_TYPE_BINARY  5

/* 最大剪贴板数据大小 */
#define CLIPBOARD_MAX_TEXT_SIZE   (64 * 1024)    /* 64KB 文本 */
#define CLIPBOARD_MAX_IMAGE_SIZE  (16 * 1024 * 1024) /* 16MB 图像 */
#define CLIPBOARD_MAX_BINARY_SIZE (32 * 1024 * 1024) /* 32MB 二进制 */

/* 剪贴板历史最大条目 */
#define CLIPBOARD_MAX_HISTORY 32

/* 剪贴板条目 */
typedef struct {
    uint32_t type;         /* 数据类型 */
    uint32_t size;         /* 数据大小 */
    void    *data;         /* 数据指针 */
    uint32_t timestamp;    /* 创建时间戳 */
    uint32_t source_pid;   /* 来源进程 PID */
    char     source_app[32]; /* 来源应用名称 */
} clipboard_entry_t;

/* 剪贴板历史 */
typedef struct {
    clipboard_entry_t entries[CLIPBOARD_MAX_HISTORY];
    uint32_t count;       /* 当前条目数 */
    uint32_t head;        /* 最新条目索引 */
    uint32_t current;     /* 当前选中条目索引 */
} clipboard_history_t;

/* 初始化剪贴板服务 */
int clipboard_service_init(void);

/* 启动剪贴板服务 */
int clipboard_service_start(void);

/* 停止剪贴板服务 */
void clipboard_service_stop(void);

/* 设置剪贴板文本 */
int clipboard_set_text(const char *text);

/* 获取剪贴板文本 */
int clipboard_get_text(char *buf, uint32_t bufsize);

/* 设置剪贴板数据 */
int clipboard_set_data(uint32_t type, const void *data, uint32_t size);

/* 获取剪贴板数据 */
int clipboard_get_data(uint32_t *type, void *buf, uint32_t *size);

/* 设置剪贴板文件列表 */
int clipboard_set_files(const char *file_list);

/* 获取剪贴板文件列表 */
int clipboard_get_files(char *buf, uint32_t bufsize);

/* 获取剪贴板数据类型 */
uint32_t clipboard_get_type(void);

/* 获取剪贴板数据大小 */
uint32_t clipboard_get_size(void);

/* 清空剪贴板 */
int clipboard_clear(void);

/* 检查剪贴板是否为空 */
int clipboard_is_empty(void);

/* 检查是否有指定类型的数据 */
int clipboard_has_type(uint32_t type);

/* ---- 剪贴板历史 ---- */

/* 获取剪贴板历史条目数 */
int clipboard_history_count(void);

/* 获取剪贴板历史条目 */
int clipboard_history_get(int index, uint32_t *type, char *buf, uint32_t bufsize);

/* 清除剪贴板历史 */
void clipboard_history_clear(void);

/* 选择历史条目作为当前剪贴板 */
int clipboard_history_select(int index);

/* ---- 拖放支持 ---- */

/* 开始拖放操作 */
int clipboard_begin_drag(int x, int y, uint32_t type, const void *data, uint32_t size);

/* 结束拖放操作 */
int clipboard_end_drag(int *target_x, int *target_y);

/* 检查是否正在拖放 */
int clipboard_is_dragging(void);

/* 获取拖放数据 */
int clipboard_get_drag_data(uint32_t *type, void *buf, uint32_t *size);

/* 设置拖放目标 */
int clipboard_set_drop_target(int accepts_type);

/* ---- 剪贴板通知 ---- */

/* 注册剪贴板变化回调 */
int clipboard_on_change(void (*callback)(uint32_t type, void *user_data), void *user_data);

/* 取消剪贴板变化回调 */
void clipboard_off_change(void);

/* 剪贴板服务更新（周期性调用） */
void clipboard_service_update(void);

#endif /* CLIPBOARD_SERVICE_H */