/* text_editor.h - FUNSOS 文本编辑器
 * 简单的图形化文本编辑工具
 */

#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

#include "stdint.h"

/* 初始化文本编辑器 */
int text_editor_init(void);

/* 运行编辑器主循环 */
void text_editor_run(void);

/* 打开文件 */
int text_editor_open(const char *path);

/* 保存文件 */
int text_editor_save(const char *path);

/* 新建文件 */
void text_editor_new(void);

#endif /* TEXT_EDITOR_H */
