/* funpack.h - .FUN 文件打包工具头文件
 * FUNSOS 归档文件打包/解包工具
 */

#ifndef FUNPACK_H
#define FUNPACK_H

#include "stdint.h"

/* ---- .FUN 归档格式定义 ---- */

#define FUNPACK_MAGIC   0x4E55462E  /* ".FUN" */
#define FUNPACK_VERSION 1

/* 归档文件头 */
typedef struct {
    uint32_t magic;           /* 魔数: 0x2E46554E */
    uint32_t version;         /* 格式版本 */
    uint32_t file_count;      /* 文件数量 */
    uint32_t index_offset;    /* 索引表偏移 */
    uint32_t data_offset;     /* 数据区偏移 */
    char     description[64]; /* 归档描述 */
} funpack_header_t;

/* 文件条目 */
typedef struct {
    char     name[64];         /* 文件名 */
    uint32_t size;             /* 原始大小 */
    uint32_t compressed_size;  /* 压缩后大小 (0 = 未压缩) */
    uint32_t offset;           /* 数据区偏移 */
    uint32_t flags;            /* 标志位 */
    uint32_t checksum;         /* 校验和 (Fletcher-32) */
} funpack_entry_t;

/* 标志位 */
#define FUNPACK_FLAG_COMPRESSED 0x01  /* 已压缩 */
#define FUNPACK_FLAG_ENCRYPTED  0x02  /* 已加密 */
#define FUNPACK_FLAG_SYMLINK    0x04  /* 符号链接 */
#define FUNPACK_FLAG_DIRECTORY  0x08  /* 目录 */

/* ---- API 函数 ---- */

/*
 * funpack_create - 创建 .FUN 归档文件
 * 参数:
 *   archive_path - 输出归档文件路径
 *   files        - 要打包的文件路径数组
 *   file_count   - 文件数量
 * 返回: 0 成功, -1 失败
 */
int funpack_create(const char *archive_path, const char **files, int file_count);

/*
 * funpack_list - 列出归档文件内容
 * 参数:
 *   archive_path - 归档文件路径
 *   entries      - 输出条目数组 (调用者分配)
 *   max_entries  - 最大条目数
 * 返回: 实际条目数, -1 失败
 */
int funpack_list(const char *archive_path, funpack_entry_t *entries, int max_entries);

/*
 * funpack_extract - 提取归档文件
 * 参数:
 *   archive_path - 归档文件路径
 *   output_dir   - 输出目录
 *   filename     - 指定文件名 (NULL = 提取全部)
 * 返回: 提取文件数, -1 失败
 */
int funpack_extract(const char *archive_path, const char *output_dir, const char *filename);

/*
 * funpack_verify - 验证归档文件完整性
 * 参数:
 *   archive_path - 归档文件路径
 * 返回: 失败的文件数, -1 无法打开/格式错误
 */
int funpack_verify(const char *archive_path);

/*
 * funpack_get_error - 获取最后一次错误信息
 * 返回: 错误描述字符串
 */
const char *funpack_get_error(void);

#endif /* FUNPACK_H */