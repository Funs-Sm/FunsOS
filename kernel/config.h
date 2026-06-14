/* config.h - 系统配置管理
 * 键值对配置存储，支持从 /etc/funsos.conf 加载
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "stdint.h"

/* 配置条目 */
typedef struct {
    char key[64];
    char value[256];
} config_entry_t;

/* 初始化配置系统 */
void config_init(void);

/* 从文件加载配置 */
int config_load(const char *path);

/* 保存配置到文件 */
int config_save(const char *path);

/* 获取字符串配置值 */
const char *config_get(const char *key, const char *default_value);

/* 设置字符串配置值 */
int config_set(const char *key, const char *value);

/* 获取整数配置值 */
int config_get_int(const char *key, int default_value);

/* 设置整数配置值 */
int config_set_int(const char *key, int value);

/* 列出所有配置项 */
void config_list(void);

/* 获取配置项数量 */
uint32_t config_count(void);

/* 删除配置项 */
int config_remove(const char *key);

/* 检查配置项是否存在 */
int config_has(const char *key);

#endif /* CONFIG_H */
