/* quota_db.h - 配额子系统 FunDB 持久化层
 *
 * 在既有 quota.c 之上加持久化：
 *   - 启动时从 FunDB 加载已存在的配额条目
 *   - set_user/set_group/remove_user/remove_group 时同步到 FunDB
 *   - sync 操作刷新使用量到 FunDB
 *   - 写入 evlog (source="Quota")
 *
 * 与 quota.c 的关系：调用 quota_get_user/quota_get_user/quota_list 读取，
 *                   然后写入 FunDB。不修改 quota.c 的内存结构。
 */
#ifndef QUOTA_DB_H
#define QUOTA_DB_H

#include "stdint.h"
#include "quota.h"

#define QUOTA_DB_PATH        "/var/db/quota.db"
#define QUOTA_TABLE_USER     "quota_user"
#define QUOTA_TABLE_GROUP    "quota_group"

/* ---- 初始化 ---- */
void quota_db_init(void);
void quota_db_shutdown(void);

/* ---- 持久化操作 ----
 * 在 quota.c 调用 set/remove 后调用相应的 db 函数。
 */
int quota_db_save_user(const quota_entry_t *e);
int quota_db_save_group(const quota_entry_t *e);
int quota_db_delete_user(uint32_t uid);
int quota_db_delete_group(uint32_t gid);

/* 启动时加载所有条目到 quota.c 的内存表 */
int quota_db_load_all(void);

/* 同步：从 quota.c 读取所有条目并写入 FunDB */
int quota_db_sync(void);

/* 查询：直接从 FunDB 读取（不依赖内存表） */
int quota_db_query_user(uint32_t uid, quota_entry_t *out);
int quota_db_query_group(uint32_t gid, quota_entry_t *out);

#endif /* QUOTA_DB_H */
