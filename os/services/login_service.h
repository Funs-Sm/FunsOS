/* login_service.h - FUNSOS 登录服务
 * 用户认证和会话管理
 */

#ifndef LOGIN_SERVICE_H
#define LOGIN_SERVICE_H

#include "stdint.h"

/* 用户信息 */
typedef struct {
    char username[32];
    uint32_t uid;
    uint8_t is_admin;
    uint8_t logged_in;
} login_user_t;

/* 初始化登录服务 */
int login_service_init(void);

/* 运行登录界面 */
void login_service_run(void);

/* 用户认证 */
int login_authenticate(const char *username, const char *password);

/* 注销 */
int login_logout(uint32_t uid);

/* 获取当前用户 */
login_user_t *login_get_current_user(void);

#endif /* LOGIN_SERVICE_H */
