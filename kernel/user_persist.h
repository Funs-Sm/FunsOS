#ifndef USER_PERSIST_H
#define USER_PERSIST_H

/* 用户数据持久化存储接口 */

void user_persist_init(void);
int  user_persist_save(void);     /* 保存用户数据到 /etc/passwd */
int  user_persist_load(void);     /* 从 /etc/passwd 加载用户数据 */
int  user_persist_save_env(void); /* 保存环境变量 */
int  user_persist_load_env(void); /* 加载环境变量 */

#endif
