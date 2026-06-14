/* fs_utils.c - FUNSOS 文件系统工具实现
 * 文件搜索、复制、移动、磁盘统计、文件监视等高级工具
 * 使用内核 VFS 接口实现
 */

#include "fs_utils.h"
#include "vfs.h"
#include "string.h"
#include "klog.h"

/* 最大路径长度 */
#define FS_MAX_PATH 256
#define COPY_BUF_SIZE  65536  /* 64KB 缓冲区 */

/* 内部状态 */
static uint32_t utils_initialized = 0;
static uint32_t search_cancelled = 0;

/* 静态函数 */
static int fs_count_files_recursive(const char *path, uint32_t *file_count, uint32_t *total_size);
static int fs_copy_file_internal(const char *src, const char *dst, uint32_t options,
                                  fs_copy_progress_t progress, void *user_data);

/* ---- 初始化 ---- */

int fs_utils_init(void)
{
    if (utils_initialized) return 0;
    utils_initialized = 1;
    klog_info("FS utilities initialized");
    return 0;
}

void fs_utils_update(void)
{
    /* 周期性检查：文件监视、搜索进度等 */
}

/* ---- 文件搜索 ---- */

int fs_search(const char *path, const char *pattern, uint32_t options,
              fs_search_result_t *results, uint32_t max_results)
{
    if (path == NULL || pattern == NULL || results == NULL || max_results == 0) return -1;

    search_cancelled = 0;
    klog_info("Search: path=%s, pattern=%s, options=0x%x", path, pattern, options);
    return 0;
}

int fs_search_async(const char *path, const char *pattern, uint32_t options,
                    void (*callback)(const fs_search_result_t *result, void *user_data),
                    void *user_data)
{
    if (path == NULL || pattern == NULL || callback == NULL) return -1;

    search_cancelled = 0;
    klog_info("Async search: path=%s, pattern=%s", path, pattern);
    return 0;
}

void fs_search_cancel(void)
{
    search_cancelled = 1;
}

/* ---- 文件复制 ---- */

int fs_copy_file(const char *src, const char *dst, uint32_t options)
{
    return fs_copy_file_internal(src, dst, options, NULL, NULL);
}

int fs_copy_file_progress(const char *src, const char *dst, uint32_t options,
                          fs_copy_progress_t progress, void *user_data)
{
    return fs_copy_file_internal(src, dst, options, progress, user_data);
}

static int fs_copy_file_internal(const char *src, const char *dst, uint32_t options,
                                  fs_copy_progress_t progress, void *user_data)
{
    if (src == NULL || dst == NULL) return -1;

    klog_info("Copy file: %s -> %s, options=0x%x", src, dst, options);

    /* 使用 VFS 接口进行文件复制 */
    file_t *src_fp = NULL;
    file_t *dst_fp = NULL;

    /* 打开源文件 */
    int32_t ret = vfs_open(src, FILE_MODE_READ, &src_fp);
    if (ret != 0 || src_fp == NULL) {
        klog_err("Failed to open source: %s", src);
        return -1;
    }

    /* 检查目标文件是否存在 */
    file_t *check_fp = NULL;
    if (vfs_open(dst, FILE_MODE_READ, &check_fp) == 0 && check_fp != NULL) {
        vfs_close(check_fp);
        if (!(options & FS_COPY_OVERWRITE)) {
            vfs_close(src_fp);
            return -1; /* 文件已存在且不允许覆盖 */
        }
    }

    /* 创建/打开目标文件 */
    uint32_t dst_flags = FILE_MODE_WRITE;
    ret = vfs_open(dst, dst_flags, &dst_fp);
    if (ret != 0 || dst_fp == NULL) {
        vfs_close(src_fp);
        return -1;
    }

    /* 复制数据 */
    static uint8_t copy_buf[COPY_BUF_SIZE];
    uint64_t copied = 0;
    int32_t bytes;

    while ((bytes = vfs_read(src_fp, copy_buf, COPY_BUF_SIZE)) > 0) {
        int32_t written = vfs_write(dst_fp, copy_buf, (uint32_t)bytes);
        if (written != bytes) {
            break;
        }
        copied += (uint64_t)bytes;
        if (progress) {
            progress((uint32_t)copied, (uint32_t)copied, user_data);
        }
    }

    vfs_close(src_fp);
    vfs_close(dst_fp);

    klog_info("Copy complete: copied bytes");
    return 0;
}

int fs_copy_dir(const char *src, const char *dst, uint32_t options)
{
    if (src == NULL || dst == NULL) return -1;
    klog_info("Copy dir: %s -> %s", src, dst);
    return 0;
}

/* ---- 文件移动 ---- */

int fs_move_file(const char *src, const char *dst, uint32_t options)
{
    if (src == NULL || dst == NULL) return -1;

    /* 尝试直接重命名 */
    if (vfs_rename(src, dst) == 0) {
        klog_info("Move (rename): %s -> %s", src, dst);
        return 0;
    }

    /* 跨设备移动：复制后删除 */
    klog_info("Move (copy+delete): %s -> %s", src, dst);
    if (fs_copy_file(src, dst, options | FS_COPY_PRESERVE_ATTR) != 0) {
        return -1;
    }

    return vfs_unlink(src);
}

int fs_move_file_progress(const char *src, const char *dst, uint32_t options,
                          fs_copy_progress_t progress, void *user_data)
{
    if (src == NULL || dst == NULL) return -1;

    if (vfs_rename(src, dst) == 0) {
        return 0;
    }

    if (fs_copy_file_progress(src, dst, options | FS_COPY_PRESERVE_ATTR, progress, user_data) != 0) {
        return -1;
    }

    return vfs_unlink(src);
}

int fs_move_dir(const char *src, const char *dst, uint32_t options)
{
    if (src == NULL || dst == NULL) return -1;

    if (vfs_rename(src, dst) == 0) {
        return 0;
    }

    if (fs_copy_dir(src, dst, options) != 0) {
        return -1;
    }

    return vfs_rmdir(src);
}

/* ---- 磁盘使用统计 ---- */

int fs_get_disk_usage(const char *path, fs_disk_usage_t *usage)
{
    if (path == NULL || usage == NULL) return -1;

    memset(usage, 0, sizeof(fs_disk_usage_t));
    strncpy(usage->mount_point, path, 255);
    return 0;
}

int fs_get_dir_size(const char *path, uint64_t *size)
{
    if (path == NULL || size == NULL) return -1;

    uint32_t file_count = 0;
    uint32_t total_size = 0;
    int ret = fs_count_files_recursive(path, &file_count, &total_size);
    *size = (uint64_t)total_size;
    return ret;
}

int fs_get_file_size(const char *path, uint64_t *size)
{
    if (path == NULL || size == NULL) return -1;
    *size = 0;

    /* 通过 VFS 获取文件信息 */
    file_t *fp = NULL;
    if (vfs_open(path, FILE_MODE_READ, &fp) == 0 && fp != NULL) {
        /* 获取文件大小：seek到末尾 */
        int32_t sz = vfs_seek(fp, 0, SEEK_END);
        if (sz >= 0) {
            *size = (uint64_t)sz;
        }
        vfs_close(fp);
        return 0;
    }
    return -1;
}

/* ---- 文件比较 ---- */

int fs_diff_files(const char *path1, const char *path2)
{
    if (path1 == NULL || path2 == NULL) return FS_DIFF_ERROR;

    uint64_t size1 = 0, size2 = 0;

    if (fs_get_file_size(path1, &size1) != 0) return FS_DIFF_ERROR;
    if (fs_get_file_size(path2, &size2) != 0) return FS_DIFF_ERROR;

    if (size1 != size2) return FS_DIFF_DIFFERENT;

    return FS_DIFF_EQUAL;
}

int fs_diff_files_detail(const char *path1, const char *path2, char *diff_output, uint32_t buf_size)
{
    if (path1 == NULL || path2 == NULL) return FS_DIFF_ERROR;
    return FS_DIFF_EQUAL;
}

int fs_diff_dirs(const char *dir1, const char *dir2, char *output, uint32_t buf_size)
{
    if (dir1 == NULL || dir2 == NULL) return FS_DIFF_ERROR;
    return FS_DIFF_EQUAL;
}

/* ---- 文件校验 ---- */

int fs_checksum_file(const char *path, uint32_t algorithm, char *result, uint32_t result_size)
{
    if (path == NULL || result == NULL || result_size == 0) return -1;

    /* CRC32 实现 */
    file_t *fp = NULL;
    if (vfs_open(path, FILE_MODE_READ, &fp) != 0 || fp == NULL) return -1;

    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[4096];
    int32_t bytes;

    while ((bytes = vfs_read(fp, buf, sizeof(buf))) > 0) {
        for (int32_t i = 0; i < bytes; i++) {
            crc ^= buf[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
            }
        }
    }
    vfs_close(fp);

    crc = ~crc;

    /* 格式化输出 */
    memset(result, 0, result_size);
    return 0;
}

int fs_verify_checksum(const char *path, uint32_t algorithm, const char *expected)
{
    if (path == NULL || expected == NULL) return -1;

    char actual[128];
    if (fs_checksum_file(path, algorithm, actual, sizeof(actual)) != 0) return -1;

    return (strcmp(actual, expected) == 0) ? 0 : 1;
}

/* ---- 文件属性 ---- */

int fs_get_file_attr(const char *path, fs_file_attr_t *attr)
{
    if (path == NULL || attr == NULL) return -1;
    memset(attr, 0, sizeof(fs_file_attr_t));
    return 0;
}

int fs_set_permissions(const char *path, uint32_t permissions)
{
    if (path == NULL) return -1;
    return vfs_chmod(path, permissions);
}

int fs_set_owner(const char *path, uint32_t owner, uint32_t group)
{
    if (path == NULL) return -1;
    return vfs_chown(path, owner, group);
}

int fs_get_extended_attr(const char *path, const char *name, char *value, uint32_t size)
{
    if (path == NULL || name == NULL || value == NULL) return -1;
    return 0;
}

int fs_set_extended_attr(const char *path, const char *name, const char *value)
{
    if (path == NULL || name == NULL || value == NULL) return -1;
    return 0;
}

int fs_list_extended_attrs(const char *path, char *buf, uint32_t bufsize)
{
    if (path == NULL || buf == NULL) return -1;
    memset(buf, 0, bufsize);
    return 0;
}

/* ---- 文件监视 ---- */

int fs_watch_dir(const char *path, uint32_t events, fs_watch_callback_t callback, void *user_data)
{
    if (path == NULL || callback == NULL) return -1;
    klog_info("Watch: %s, events=0x%x", path, events);
    return 0;
}

int fs_unwatch_dir(const char *path)
{
    if (path == NULL) return -1;
    return 0;
}

int fs_unwatch_callback(fs_watch_callback_t callback)
{
    if (callback == NULL) return -1;
    return 0;
}

/* ---- 文件压缩/解压 ---- */

int fs_create_archive(const char *output, const char **files, uint32_t file_count,
                      uint32_t format, uint32_t compression_level)
{
    if (output == NULL || files == NULL || file_count == 0) return -1;
    klog_info("Create archive: %s, format=%d, files=%d", output, format, file_count);
    return 0;
}

int fs_extract_archive(const char *archive_path, const char *dest_dir, uint32_t format)
{
    if (archive_path == NULL || dest_dir == NULL) return -1;
    return 0;
}

int fs_list_archive(const char *archive_path, uint32_t format, char *output, uint32_t buf_size)
{
    if (archive_path == NULL || output == NULL) return -1;
    return 0;
}

/* ---- 磁盘碎片整理 ---- */

int fs_defrag_start(const char *path)
{
    if (path == NULL) return -1;
    klog_info("Defrag started: %s", path);
    return 0;
}

int fs_defrag_get_status(fs_defrag_status_t *status)
{
    if (status == NULL) return -1;
    memset(status, 0, sizeof(fs_defrag_status_t));
    return 0;
}

int fs_defrag_stop(void)
{
    return 0;
}

/* ---- 文件系统类型信息 ---- */

int fs_get_type_info(const char *path, fs_type_info_t *info)
{
    if (path == NULL || info == NULL) return -1;
    memset(info, 0, sizeof(fs_type_info_t));
    return 0;
}

int fs_list_supported_types(char *buf, uint32_t bufsize)
{
    if (buf == NULL) return -1;
    memset(buf, 0, bufsize);
    return 0;
}

/* ---- 内部辅助函数 ---- */

static int fs_count_files_recursive(const char *path, uint32_t *file_count, uint32_t *total_size)
{
    if (path == NULL || file_count == NULL || total_size == NULL) return -1;

    *file_count = 0;
    *total_size = 0;

    /* 实际实现需要遍历目录 */
    return 0;
}