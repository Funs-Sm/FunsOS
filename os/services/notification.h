/* notification.h - FUNSOS 通知服务
 * 系统通知的显示和管理
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include "stdint.h"

/* 通知类型 */
#define NOTIFY_INFO      0
#define NOTIFY_WARNING   1
#define NOTIFY_ERROR     2
#define NOTIFY_SUCCESS   3

/* 通知结构 */
typedef struct {
    char title[64];
    char message[256];
    uint32_t type;
    uint32_t duration_ms;
    uint32_t created_tick;
} notification_t;

/* 初始化通知服务 */
int notification_init(void);

/* 发送通知 */
int notification_send(const char *title, const char *message, uint32_t type);

/* 发送带持续时间通知 */
int notification_send_timed(const char *title, const char *message,
                            uint32_t type, uint32_t duration_ms);

/* 渲染通知 */
void notification_render(void);

/* 更新通知（移除过期的） */
void notification_update(void);

/* 清除所有通知 */
void notification_clear_all(void);

#endif /* NOTIFICATION_H */
