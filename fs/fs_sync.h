#ifndef FS_SYNC_H
#define FS_SYNC_H

#include "stdint.h"
#include "vfs.h"

/* ============================================================
 * File System Synchronization Subsystem
 * 文件系统同步子系统
 *
 * 功能：
 * - 脏 inode 跟踪（需要写回的 inode）
 * - sync：同步所有文件系统
 * - fsync：同步单个文件
 * - fdatasync：只同步文件数据（不同步元数据）
 * - syncfs：同步单个文件系统
 * - 同步统计
 * ============================================================ */

/* 同步统计信息 */
typedef struct fs_sync_stats {
    uint64_t sync_calls;         /* sync() 调用次数 */
    uint64_t fsync_calls;        /* fsync() 调用次数 */
    uint64_t fdatasync_calls;    /* fdatasync() 调用次数 */
    uint64_t syncfs_calls;       /* syncfs() 调用次数 */
    uint64_t inodes_synced;      /* 已同步的 inode 数 */
    uint64_t blocks_synced;      /* 已同步的块数 */
    uint64_t dirty_inodes;       /* 当前脏 inode 数 */
    uint64_t max_dirty_inodes;   /* 历史最大脏 inode 数 */
} fs_sync_stats_t;

/* ---- 初始化 ---- */
void fs_sync_init(void);

/* ---- 核心 API ---- */

/* 标记 inode 为脏（需要同步）*/
void fs_sync_mark_dirty(inode_t *inode);

/* 标记 inode 为干净（已同步）*/
void fs_sync_mark_clean(inode_t *inode);

/* 检查 inode 是否为脏 */
int fs_sync_is_dirty(inode_t *inode);

/* 同步单个文件（数据 + 元数据）*/
int32_t fs_sync_fsync(file_t *file);

/* 同步单个文件（仅数据）*/
int32_t fs_sync_fdatasync(file_t *file);

/* 同步所有脏 inode */
int32_t fs_sync_all(void);

/* 同步指定超级块的所有脏 inode */
int32_t fs_sync_sb(superblock_t *sb);

/* ---- 统计 ---- */

/* 获取同步统计信息 */
void fs_sync_get_stats(fs_sync_stats_t *stats);

/* 重置统计信息 */
void fs_sync_reset_stats(void);

#endif /* FS_SYNC_H */
