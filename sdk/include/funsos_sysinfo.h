#ifndef FUNSOS_SYSINFO_H
#define FUNSOS_SYSINFO_H

/*
 * FUNSOS 系统信息 API
 * 提供系统版本、内存状态、运行时间等信息查询。
 * 基于 kernel/sys_api.h 和 kernel/version.h。
 */

#include "stdint.h"

/* ---- 系统信息结构体 ---- */
typedef struct {
    char     os_name[32];       /* 操作系统名称 */
    char     kernel_name[32];   /* 内核名称 */
    char     kernel_version[16]; /* 内核版本 */
    uint32_t total_memory;      /* 总内存 (KB) */
    uint32_t used_memory;       /* 已用内存 (KB) */
    uint32_t cpu_count;         /* CPU 核心数 */
    uint32_t uptime;            /* 运行时间 (秒) */
    uint32_t process_count;     /* 进程数 */
} funsos_sysinfo_t;

/*
 * 获取系统时钟滴答数
 * 返回: 自启动以来的时钟滴答数
 */
uint32_t funsos_get_ticks(void);

/*
 * 获取内核版本字符串
 * 返回: 版本字符串指针（静态存储，无需释放）
 */
const char *funsos_get_version(void);

/*
 * 获取内存信息
 * 参数: total - 接收总内存 (KB); used - 接收已用内存 (KB)
 * 返回: 0 成功, -1 失败
 */
int funsos_get_memory_info(uint32_t *total, uint32_t *used);

/*
 * 获取完整系统信息
 * 参数: info - 接收系统信息的结构体指针
 * 返回: 0 成功, -1 失败
 */
int funsos_get_sysinfo(funsos_sysinfo_t *info);

/*
 * 获取当前时间（自 Unix 纪元以来的秒数）
 * 返回: Unix 时间戳
 */
uint32_t funsos_get_time(void);

#endif /* FUNSOS_SYSINFO_H */
