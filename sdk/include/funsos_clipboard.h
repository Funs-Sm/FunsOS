/* funsos_clipboard.h - 剪贴板 API
 * 系统剪贴板操作：文本、图像、文件列表的复制粘贴
 */

#ifndef FUNSOS_CLIPBOARD_H
#define FUNSOS_CLIPBOARD_H

#include "stdint.h"

/* ---- 剪贴板数据类型 ---- */
typedef enum {
    FUNSOS_CLIPBOARD_TEXT  = 0,  /* 纯文本 */
    FUNSOS_CLIPBOARD_RTF   = 1,  /* 富文本 */
    FUNSOS_CLIPBOARD_HTML  = 2,  /* HTML 内容 */
    FUNSOS_CLIPBOARD_IMAGE = 3,  /* 位图图像 */
    FUNSOS_CLIPBOARD_FILES = 4,  /* 文件列表 */
    FUNSOS_CLIPBOARD_BINARY = 5  /* 二进制数据 */
} funsos_clipboard_type_t;

/* ---- 剪贴板数据格式 ---- */
typedef struct {
    funsos_clipboard_type_t type;
    uint32_t size;              /* 数据大小（字节） */
    void    *data;              /* 数据指针 */
    uint32_t width;             /* 图像宽度（仅图像类型） */
    uint32_t height;            /* 图像高度（仅图像类型） */
    uint32_t bpp;               /* 图像位深度（仅图像类型） */
} funsos_clipboard_data_t;

/* ---- 文本剪贴板 ---- */

/* 设置剪贴板文本 */
int funsos_clipboard_set_text(const char *text);

/* 获取剪贴板文本（返回实际长度，-1 表示失败） */
int funsos_clipboard_get_text(char *buf, uint32_t bufsize);

/* 检查剪贴板中是否有文本 */
int funsos_clipboard_has_text(void);

/* ---- 通用剪贴板 ---- */

/* 设置剪贴板数据（任意类型） */
int funsos_clipboard_set_data(const funsos_clipboard_data_t *data);

/* 获取剪贴板数据 */
int funsos_clipboard_get_data(funsos_clipboard_data_t *data);

/* 获取剪贴板当前数据类型 */
funsos_clipboard_type_t funsos_clipboard_get_type(void);

/* 获取剪贴板数据大小 */
uint32_t funsos_clipboard_get_size(void);

/* ---- 剪贴板管理 ---- */

/* 清空剪贴板 */
int funsos_clipboard_clear(void);

/* 检查剪贴板是否为空 */
int funsos_clipboard_is_empty(void);

/* 检查是否有指定类型的数据 */
int funsos_clipboard_has_type(funsos_clipboard_type_t type);

/* 获取剪贴板中所有可用类型 */
int funsos_clipboard_get_types(funsos_clipboard_type_t *types, uint32_t max_types);

/* ---- 文件列表剪贴板 ---- */

/* 设置剪贴板文件列表（路径以 '\n' 分隔） */
int funsos_clipboard_set_files(const char *file_list);

/* 获取剪贴板文件列表 */
int funsos_clipboard_get_files(char *buf, uint32_t bufsize);

/* 获取文件数量 */
int funsos_clipboard_get_file_count(void);

#endif /* FUNSOS_CLIPBOARD_H */