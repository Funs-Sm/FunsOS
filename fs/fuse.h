#ifndef FUSE_H
#define FUSE_H

#include "vfs.h"
#include "stdint.h"

/* FUSE 操作码 */
#define FUSE_LOOKUP    1
#define FUSE_FORGET    2
#define FUSE_GETATTR   3
#define FUSE_SETATTR   4
#define FUSE_READLINK  5
#define FUSE_SYMLINK   6
#define FUSE_MKNOD     8
#define FUSE_MKDIR     9
#define FUSE_UNLINK   10
#define FUSE_RMDIR    11
#define FUSE_RENAME   12
#define FUSE_OPEN     14
#define FUSE_READ     15
#define FUSE_WRITE    16
#define FUSE_RELEASE  18
#define FUSE_FSYNC    20
#define FUSE_INIT     26

/* FUSE 请求结构 */
typedef struct {
    uint32_t unique;     /* 唯一ID */
    uint32_t opcode;     /* 操作码 */
    uint64_t nodeid;     /* 节点ID */
    uint32_t uid, gid;
    uint32_t pid;
    uint32_t data_len;
    char     data[4096]; /* 请求数据 */
} fuse_request_t;

/* FUSE 响应结构 */
typedef struct {
    uint32_t unique;
    int32_t  error;      /* 0=成功, 负数=错误 */
    uint32_t data_len;
    char     data[4096];
} fuse_response_t;

/* FUSE 挂载实例 */
typedef struct fuse_mount {
    char mount_point[256];     /* 挂载路径 */
    char daemon_path[256];     /* 守护进程路径 */
    superblock_t *sb;          /* 关联的超级块 */
    uint32_t next_unique;      /* 下一个请求唯一ID */
    int active;                /* 是否活跃 */
    struct fuse_mount *next;   /* 链表下一个 */
} fuse_mount_t;

/* FUSE 文件系统注册 */
int fuse_register_fs(const char *mount_point, const char *daemon_path);
int fuse_unregister_fs(const char *mount_point);

/* FUSE 守护进程通信 */
int fuse_send_request(fuse_request_t *req, fuse_response_t *resp);

/* VFS 接口 */
int fuse_mount(superblock_t *sb, void *data);

/* 初始化 */
void fuse_init(void);

#endif /* FUSE_H */
