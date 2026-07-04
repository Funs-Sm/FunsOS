#ifndef INOTIFY_H
#define INOTIFY_H

#include "stdint.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"

/* ============================================================
 * Inotify (Inode Notify) 子系统
 *
 * 提供文件系统事件通知机制。
 * 类似 Linux inotify，监控文件和目录的变化。
 * ============================================================ */

/* Inotify 事件类型 */
#define IN_ACCESS        0x00000001  /* 文件被访问 */
#define IN_MODIFY        0x00000002  /* 文件被修改 */
#define IN_ATTRIB        0x00000004  /* 文件属性变化 */
#define IN_CLOSE_WRITE   0x00000008  /* 可写文件被关闭 */
#define IN_CLOSE_NOWRITE 0x00000010  /* 只读文件被关闭 */
#define IN_OPEN          0x00000020  /* 文件被打开 */
#define IN_MOVED_FROM    0x00000040  /* 文件被移走 */
#define IN_MOVED_TO      0x00000080  /* 文件被移入 */
#define IN_CREATE        0x00000100  /* 文件被创建 */
#define IN_DELETE        0x00000200  /* 文件被删除 */
#define IN_DELETE_SELF   0x00000400  /* 自身被删除 */
#define IN_MOVE_SELF     0x00000800  /* 自身被移动 */

/* 辅助事件 */
#define IN_UNMOUNT       0x00002000  /* 文件系统被卸载 */
#define IN_Q_OVERFLOW    0x00004000  /* 事件队列溢出 */
#define IN_IGNORED       0x00008000  /* 监控被移除 */
#define IN_CLOSE        (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_MOVE         (IN_MOVED_FROM | IN_MOVED_TO)

/* 特殊标志 */
#define IN_ONLYDIR       0x01000000  /* 仅监控目录 */
#define IN_DONT_FOLLOW   0x02000000  /* 不跟随符号链接 */
#define IN_EXCL_UNLINK   0x04000000  /* 取消链接后排除事件 */
#define IN_MASK_ADD      0x20000000  /* 追加 mask */
#define IN_ISDIR         0x40000000  /* 事件发生在目录 */
#define IN_ONESHOT       0x80000000  /* 一次性监控 */

/* 最大 inotify 实例数和 watch 数 */
#define INOTIFY_MAX_INSTANCES  32
#define INOTIFY_MAX_WATCHES    1024
#define INOTIFY_MAX_EVENTS     256
#define INOTIFY_NAME_MAX       256

/* Inotify 事件结构 */
typedef struct inotify_event {
    int32_t  wd;         /* Watch 描述符 */
    uint32_t mask;       /* 事件掩码 */
    uint32_t cookie;     /* 关联 cookie (用于 rename) */
    uint32_t len;        /* name 字段长度 */
    char     name[];     /* 可选的文件名 */
} inotify_event_t;

/* Inotify watch 条目 */
typedef struct inotify_watch {
    int              wd;           /* Watch 描述符 */
    char             path[512];    /* 监控路径 */
    uint32_t         mask;         /* 监控掩码 */
    int              active;       /* 是否激活 */
    int              oneshot;      /* 一次性 */
    struct inotify_watch *next;
    struct inotify_watch *prev;
    void            *instance;     /* 所属实例 */
} inotify_watch_t;

/* Inotify 事件队列节点 */
typedef struct inotify_event_node {
    inotify_event_t  *event;
    struct inotify_event_node *next;
} inotify_event_node_t;

/* Inotify 实例 */
typedef struct inotify_instance {
    int                  id;           /* 实例 ID */
    int                  used;         /* 是否使用 */
    uint32_t             watch_count;  /* 监控数量 */
    inotify_watch_t     *watch_list;   /* 监控链表 */
    inotify_event_node_t *event_head;  /* 事件队列头 */
    inotify_event_node_t *event_tail;  /* 事件队列尾 */
    uint32_t             event_count;  /* 事件数量 */
    uint32_t             max_events;   /* 最大事件数 */
    spinlock_t           lock;
    sem_t                event_sem;    /* 事件信号量 */
    int                  next_wd;      /* 下一个 wd */
} inotify_instance_t;

/* ============================================================
 * 初始化
 * ============================================================ */
void inotify_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

/*
 * inotify_create - 创建 inotify 实例
 * flags: 保留 (未来支持 IN_NONBLOCK, IN_CLOEXEC)
 * 返回: inotify 文件描述符, 负数错误码
 */
int inotify_create(int flags);

/*
 * inotify_add_watch - 添加监控
 * fd:   inotify 实例描述符
 * path: 监控路径
 * mask: 监控事件掩码
 * 返回: watch 描述符, 负数错误码
 */
int inotify_add_watch(int fd, const char *path, uint32_t mask);

/*
 * inotify_rm_watch - 移除监控
 * fd: inotify 实例描述符
 * wd: watch 描述符
 * 返回: 0 成功, 负数错误码
 */
int inotify_rm_watch(int fd, int wd);

/*
 * inotify_read - 读取事件
 * fd:     inotify 实例描述符
 * buf:    输出缓冲区
 * count:  缓冲区大小
 * 返回: 读取的字节数, 负数错误码
 */
int inotify_read(int fd, void *buf, uint32_t count);

/* ============================================================
 * 事件生成接口 (供 VFS 等子系统调用)
 * ============================================================ */

/*
 * inotify_dispatch_event - 分发文件系统事件
 * path: 发生事件的路径
 * mask: 事件类型
 * name: 相关文件名 (可为 NULL)
 * cookie: 关联 cookie
 */
void inotify_dispatch_event(const char *path, uint32_t mask,
                            const char *name, uint32_t cookie);

/* ============================================================
 * 系统调用接口
 * ============================================================ */
int sys_inotify_init(void);
int sys_inotify_init1(int flags);
int sys_inotify_add_watch(int fd, const char *path, uint32_t mask);
int sys_inotify_rm_watch(int fd, int wd);

/* ============================================================
 * 调试与统计
 * ============================================================ */
void inotify_dump(int fd);
int inotify_get_instance_count(void);

#endif /* INOTIFY_H */
