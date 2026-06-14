/* fs_utils.h - FUNSOS 文件系统工具
 * 文件搜索、复制、移动、磁盘使用统计、权限管理等高级工具
 */

#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "stdint.h"

/* ---- 文件搜索相关 ---- */

/* 搜索选项 */
#define FS_SEARCH_RECURSIVE   0x01  /* 递归搜索子目录 */
#define FS_SEARCH_FILES       0x02  /* 包含文件 */
#define FS_SEARCH_DIRS        0x04  /* 包含目录 */
#define FS_SEARCH_CASE_SENS   0x08  /* 区分大小写 */
#define FS_SEARCH_REGEX       0x10  /* 使用正则表达式 */
#define FS_SEARCH_HIDDEN      0x20  /* 包含隐藏文件 */

/* 搜索结果 */
#define FS_SEARCH_MAX_RESULTS 1024

typedef struct {
    char     path[256];      /* 文件路径 */
    uint32_t size;           /* 文件大小 */
    uint32_t modified;       /* 修改时间 */
    uint32_t type;           /* 文件类型（0=文件, 1=目录, 2=符号链接） */
    uint32_t permissions;    /* 权限 */
} fs_search_result_t;

/* 搜索文件 */
int fs_search(const char *path, const char *pattern, uint32_t options,
              fs_search_result_t *results, uint32_t max_results);

/* 搜索文件（回调模式） */
int fs_search_async(const char *path, const char *pattern, uint32_t options,
                    void (*callback)(const fs_search_result_t *result, void *user_data),
                    void *user_data);

/* 取消搜索 */
void fs_search_cancel(void);

/* ---- 文件复制/移动 ---- */

/* 复制选项 */
#define FS_COPY_OVERWRITE    0x01  /* 覆盖已存在文件 */
#define FS_COPY_RECURSIVE    0x02  /* 递归复制目录 */
#define FS_COPY_PRESERVE_ATTR 0x04 /* 保留文件属性 */
#define FS_COPY_VERIFY       0x08  /* 复制后验证 */

/* 复制进度回调 */
typedef void (*fs_copy_progress_t)(uint32_t copied, uint32_t total, void *user_data);

/* 复制文件 */
int fs_copy_file(const char *src, const char *dst, uint32_t options);

/* 复制文件（带进度回调） */
int fs_copy_file_progress(const char *src, const char *dst, uint32_t options,
                          fs_copy_progress_t progress, void *user_data);

/* 复制目录 */
int fs_copy_dir(const char *src, const char *dst, uint32_t options);

/* 移动文件 */
int fs_move_file(const char *src, const char *dst, uint32_t options);

/* 移动文件（带进度回调） */
int fs_move_file_progress(const char *src, const char *dst, uint32_t options,
                          fs_copy_progress_t progress, void *user_data);

/* 移动目录 */
int fs_move_dir(const char *src, const char *dst, uint32_t options);

/* ---- 磁盘使用统计 ---- */

/* 磁盘使用信息 */
typedef struct {
    char     mount_point[256];  /* 挂载点 */
    char     fs_type[32];       /* 文件系统类型 */
    uint64_t total_bytes;       /* 总容量 */
    uint64_t used_bytes;        /* 已用容量 */
    uint64_t free_bytes;        /* 可用容量 */
    uint64_t available_bytes;   /* 非特权用户可用容量 */
    uint32_t block_size;        /* 块大小 */
    uint32_t total_inodes;      /* 总 inode 数 */
    uint32_t free_inodes;       /* 可用 inode 数 */
    uint32_t used_percent;      /* 使用百分比 */
    uint8_t  read_only;         /* 是否只读 */
} fs_disk_usage_t;

/* 获取磁盘使用信息 */
int fs_get_disk_usage(const char *path, fs_disk_usage_t *usage);

/* 获取目录大小 */
int fs_get_dir_size(const char *path, uint64_t *size);

/* 获取文件大小 */
int fs_get_file_size(const char *path, uint64_t *size);

/* ---- 文件比较 ---- */

/* 比较结果 */
#define FS_DIFF_EQUAL     0
#define FS_DIFF_DIFFERENT 1
#define FS_DIFF_ERROR     2

/* 比较两个文件 */
int fs_diff_files(const char *path1, const char *path2);

/* 比较两个文件（详细输出） */
int fs_diff_files_detail(const char *path1, const char *path2, char *diff_output, uint32_t buf_size);

/* 比较两个目录 */
int fs_diff_dirs(const char *dir1, const char *dir2, char *output, uint32_t buf_size);

/* ---- 文件校验 ---- */

/* 校验算法 */
#define FS_CHECKSUM_MD5      0
#define FS_CHECKSUM_SHA1     1
#define FS_CHECKSUM_SHA256   2
#define FS_CHECKSUM_CRC32    3

/* 计算文件校验和 */
int fs_checksum_file(const char *path, uint32_t algorithm, char *result, uint32_t result_size);

/* 验证文件校验和 */
int fs_verify_checksum(const char *path, uint32_t algorithm, const char *expected);

/* ---- 文件属性 ---- */

/* 文件属性 */
typedef struct {
    uint32_t size;          /* 文件大小 */
    uint32_t created;       /* 创建时间 */
    uint32_t modified;      /* 修改时间 */
    uint32_t accessed;      /* 访问时间 */
    uint32_t permissions;   /* 权限 */
    uint32_t owner;         /* 所有者 */
    uint32_t group;         /* 组 */
    uint32_t type;          /* 类型 */
    uint32_t links;         /* 硬链接数 */
    uint32_t device;        /* 设备号 */
    char     ext_attrs[256]; /* 扩展属性 */
} fs_file_attr_t;

/* 获取文件属性 */
int fs_get_file_attr(const char *path, fs_file_attr_t *attr);

/* 设置文件权限 */
int fs_set_permissions(const char *path, uint32_t permissions);

/* 设置文件所有者 */
int fs_set_owner(const char *path, uint32_t owner, uint32_t group);

/* 获取文件扩展属性 */
int fs_get_extended_attr(const char *path, const char *name, char *value, uint32_t size);

/* 设置文件扩展属性 */
int fs_set_extended_attr(const char *path, const char *name, const char *value);

/* 列出扩展属性 */
int fs_list_extended_attrs(const char *path, char *buf, uint32_t bufsize);

/* ---- 文件监视 ---- */

/* 监视事件 */
#define FS_WATCH_CREATE   0x01
#define FS_WATCH_DELETE   0x02
#define FS_WATCH_MODIFY   0x04
#define FS_WATCH_RENAME   0x08
#define FS_WATCH_ATTRIB   0x10
#define FS_WATCH_ALL      0xFF

/* 监视回调 */
typedef void (*fs_watch_callback_t)(uint32_t event, const char *path, void *user_data);

/* 开始监视目录 */
int fs_watch_dir(const char *path, uint32_t events, fs_watch_callback_t callback, void *user_data);

/* 停止监视目录 */
int fs_unwatch_dir(const char *path);

/* 停止监视回调 */
int fs_unwatch_callback(fs_watch_callback_t callback);

/* ---- 文件压缩/解压 ---- */

/* 压缩格式 */
#define FS_ARCHIVE_TAR   0
#define FS_ARCHIVE_ZIP   1
#define FS_ARCHIVE_GZIP  2
#define FS_ARCHIVE_BZIP2 3

/* 创建压缩包 */
int fs_create_archive(const char *output, const char **files, uint32_t file_count,
                      uint32_t format, uint32_t compression_level);

/* 解压压缩包 */
int fs_extract_archive(const char *archive_path, const char *dest_dir, uint32_t format);

/* 列出压缩包内容 */
int fs_list_archive(const char *archive_path, uint32_t format, char *output, uint32_t buf_size);

/* ---- 磁盘碎片整理 ---- */

/* 碎片整理状态 */
typedef struct {
    uint32_t percent;         /* 进度百分比 */
    uint32_t files_processed; /* 已处理文件数 */
    uint32_t fragments_fixed; /* 已修复碎片数 */
    uint8_t  running;         /* 是否正在运行 */
} fs_defrag_status_t;

/* 开始碎片整理 */
int fs_defrag_start(const char *path);

/* 获取碎片整理状态 */
int fs_defrag_get_status(fs_defrag_status_t *status);

/* 停止碎片整理 */
int fs_defrag_stop(void);

/* ---- 文件系统信息 ---- */

/* 文件系统类型信息 */
typedef struct {
    char     name[32];          /* 文件系统名称 */
    char     description[128];  /* 描述 */
    uint32_t max_file_size_mb;  /* 最大文件大小 (MB) */
    uint32_t max_volume_size_gb; /* 最大卷大小 (GB) */
    uint32_t max_filename_len;  /* 最大文件名长度 */
    uint8_t  supports_journal;  /* 是否支持日志 */
    uint8_t  supports_symlinks; /* 是否支持符号链接 */
    uint8_t  supports_permissions; /* 是否支持权限 */
    uint8_t  supports_compression; /* 是否支持压缩 */
    uint8_t  supports_encryption; /* 是否支持加密 */
    uint8_t  supports_snapshots; /* 是否支持快照 */
} fs_type_info_t;

/* 获取文件系统类型信息 */
int fs_get_type_info(const char *path, fs_type_info_t *info);

/* 获取所有支持的文件系统类型 */
int fs_list_supported_types(char *buf, uint32_t bufsize);

/* ---- 文件系统工具状态 ---- */

/* 初始化文件系统工具 */
int fs_utils_init(void);

/* 文件系统工具更新（周期性调用） */
void fs_utils_update(void);

#endif /* FS_UTILS_H */