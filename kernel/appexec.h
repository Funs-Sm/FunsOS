/* appexec.h - 应用执行服务
 *
 * 将注册表中的应用真正启动起来：
 *   - 从 HKLM\Software\Apps\<id> 读取应用信息（name/path/command/category）
 *   - 记录启动历史（循环缓冲）
 *   - 启动/完成事件写入 evlog（source="AppExec"）
 *
 * 与 shell 的关系：
 *   shell 的 "apps run <id>" 命令调用 appexec_get_info() 获取命令，
 *   执行后调用 appexec_record_result() 记录结果。
 *   "apps history" 调用 appexec_history() 显示启动历史。
 */
#ifndef APPEXEC_H
#define APPEXEC_H

#include "stdint.h"
#include "stddef.h"

#define APPEXEC_MAX_ID        32
#define APPEXEC_MAX_NAME      64
#define APPEXEC_MAX_CMD       256
#define APPEXEC_MAX_CATEGORY  32
#define APPEXEC_HISTORY_SIZE  32

/* 应用信息（从注册表读取） */
typedef struct {
    char id[APPEXEC_MAX_ID];           /* 应用 ID */
    char name[APPEXEC_MAX_NAME];       /* 显示名 */
    char path[APPEXEC_MAX_CMD];        /* 可执行路径 */
    char command[APPEXEC_MAX_CMD];     /* 执行命令（可能为空，则用 path 或 id） */
    char category[APPEXEC_MAX_CATEGORY];
    char version[32];
    char description[128];
} appexec_app_info_t;

/* 启动历史条目 */
typedef struct {
    char     id[APPEXEC_MAX_ID];
    char     name[APPEXEC_MAX_NAME];
    char     command[APPEXEC_MAX_CMD];
    uint64_t launch_tick;              /* 启动时刻（ticks） */
    uint32_t duration_ms;              /* 执行时长（ms），0=仍在运行/未知 */
    int32_t  exit_code;                /* 退出码 */
    uint8_t  completed;                /* 是否已完成 */
} appexec_history_t;

/* 统计 */
typedef struct {
    uint32_t total_launches;
    uint32_t successful;
    uint32_t failed;
    uint32_t unique_apps;
} appexec_stats_t;

/* ---- 初始化 ---- */
void appexec_init(void);
void appexec_shutdown(void);

/* ---- 应用信息查询 ---- */
/* 从注册表读取应用信息。返回 0=成功，-1=未找到 */
int appexec_get_info(const char *app_id, appexec_app_info_t *out);

/* ---- 启动与记录 ---- */
/* 记录应用启动（在执行命令前调用）。返回分配的历史索引，<0=失败 */
int appexec_record_launch(const char *app_id, const char *app_name,
                          const char *command);

/* 记录应用执行结果（在命令执行完成后调用） */
void appexec_record_result(int history_index, int32_t exit_code);

/* 便捷函数：执行应用并自动记录。
 * command_override 非空时覆盖注册表中的命令。
 * 返回退出码。此函数会调用 shell_execute()。
 * 注意：调用者需确保 shell_execute 可用（即 shell.c 中定义此函数） */
int appexec_run(const char *app_id, const char *command_override);

/* ---- 历史 ---- */
uint32_t appexec_history_count(void);
/* 返回历史条目（最新在前），返回实际写入数量 */
uint32_t appexec_history(appexec_history_t *out, uint32_t max_count);
void appexec_clear_history(void);

/* ---- 统计 ---- */
void appexec_get_stats(appexec_stats_t *stats);
void appexec_reset_stats(void);

#endif /* APPEXEC_H */
