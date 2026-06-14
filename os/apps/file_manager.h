/* file_manager.h - FUNSOS 文件管理器
 * 图形化文件浏览和管理工具
 */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "stdint.h"

/* 文件项 */
typedef struct {
    char name[128];
    uint32_t size;
    uint32_t is_dir;
    uint32_t modified_time;
} fm_entry_t;

/* 初始化文件管理器 */
int file_manager_init(void);

/* 运行文件管理器主循环 */
void file_manager_run(void);

/* 导航 */
int file_manager_navigate(const char *path);
int file_manager_go_up(void);
int file_manager_refresh(void);

/* 文件操作 */
int file_manager_copy(const char *src, const char *dst);
int file_manager_move(const char *src, const char *dst);
int file_manager_delete(const char *path);
int file_manager_mkdir(const char *path);
int file_manager_rename(const char *old_name, const char *new_name);

/* 获取当前目录内容 */
fm_entry_t *file_manager_get_entries(uint32_t *count);

#endif /* FILE_MANAGER_H */
