/* settings.h - FUNSOS 系统设置
 * 图形化系统配置工具，支持显示、鼠标、键盘、日期时间和关于信息
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "stdint.h"

/* 设置分类 */
#define SETTINGS_CAT_DISPLAY  0
#define SETTINGS_CAT_MOUSE    1
#define SETTINGS_CAT_KEYBOARD 2
#define SETTINGS_CAT_DATETIME 3
#define SETTINGS_CAT_ABOUT    4
#define SETTINGS_CAT_COUNT    5

/* 初始化设置界面 */
int settings_init(void);

/* 运行设置主循环 */
void settings_run(void);

/* 设置分类快捷入口 */
int settings_show_display(void);
int settings_show_mouse(void);
int settings_show_keyboard(void);
int settings_show_datetime(void);
int settings_show_about(void);

/* 获取/设置配置值 */
int  settings_get_int(const char *key, int default_val);
void settings_set_int(const char *key, int value);
const char *settings_get_str(const char *key, const char *default_val);
void settings_set_str(const char *key, const char *value);

#endif /* SETTINGS_H */