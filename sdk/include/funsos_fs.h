#ifndef FUNSOS_FS_H
#define FUNSOS_FS_H

/*
 * FUNSOS 文件系统高级操作 API
 * 提供目录遍历、文件监控、属性操作、文件系统操作等高级功能。
 * 基于 kernel/vfs.h 和 fs/vfs_advanced.h 的系统调用封装。
 */

#include "stdint.h"
#include "funsos_stat.h"
#include "funsos_dir.h"

/* ---- 文件监控事件类型 ---- */
#define FUNSOS_IN_ACCESS       0x0001   /* 文件被访问 */
#define FUNSOS_IN_MODIFY       0x0002   /* 文件被修改 */
#define FUNSOS_IN_ATTRIB       0x0004   /* 文件属性变化 */
#define FUNSOS_IN_CLOSE_WRITE  0x0008   /* 可写文件被关闭 */
#define FUNSOS_IN_CLOSE_NOWRITE 0x0010  /* 只读文件被关闭 */
#define FUNSOS_IN_OPEN         0x0020   /* 文件被打开 */
#define FUNSOS_IN_MOVED_FROM   0x0040   /* 文件被移走 */
#define FUNSOS_IN_MOVED_TO     0x0080   /* 文件被移入 */
#define FUNSOS_IN_CREATE       0x0100   /* 文件被创建 */
#define FUNSOS_IN_DELETE       0x0200   /* 文件被删除 */
#define FUNSOS_IN_DELETE_SELF  0x0400   /* 监控的文件/目录自身被删除 */
#define FUNSOS_IN_MOVE_SELF    0x0800   /* 监控的文件/目录自身被移动 */

/* 文件监控事件结构 */
typedef struct {
    int         wd;           /* 监控描述符 */
    uint32_t    mask;         /* 事件掩码 */
    uint32_t    cookie;       /* 关联事件的 cookie (如 move) */
    uint32_t    len;          /* 名称长度 */
    char        name[256];    /* 文件名（可变长） */
} funsos_inotify_event_t;

/* ---- 文件属性操作 ---- */

/* 文件属性类型 */
typedef enum {
    FUNSOS_XATTR_CREATE   = 1,   /* 只创建新属性，若存在则失败 */
    FUNSOS_XATTR_REPLACE  = 2    /* 只替换已存在的属性，若不存在则失败 */
} funsos_xattr_flag_t;

/*
 * 初始化文件监控实例
 * 返回: 文件监控实例描述符, -1 失败
 */
int funsos_inotify_init(void);

/*
 * 添加监控
 * 参数: fd - inotify 实例描述符; pathname - 要监控的路径; mask - 事件掩码
 * 返回: 监控描述符 (wd), -1 失败
 */
int funsos_inotify_add_watch(int fd, const char *pathname, uint32_t mask);

/*
 * 移除监控
 * 参数: fd - inotify 实例描述符; wd - 监控描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_inotify_rm_watch(int fd, int wd);

/*
 * 读取监控事件
 * 参数: fd - inotify 实例描述符; buf - 接收事件的缓冲区; len - 缓冲区大小
 * 返回: 读取的字节数, -1 失败
 */
int funsos_inotify_read(int fd, void *buf, uint32_t len);

/* ---- 扩展属性 ---- */

/*
 * 设置扩展属性
 * 参数: path - 文件路径; name - 属性名; value - 属性值; size - 属性值大小; flags - 标志
 * 返回: 0 成功, -1 失败
 */
int funsos_setxattr(const char *path, const char *name, 
                    const void *value, uint32_t size, int flags);

/*
 * 设置扩展属性（通过文件描述符）
 * 参数: fd - 文件描述符; name - 属性名; value - 属性值; size - 属性值大小; flags - 标志
 * 返回: 0 成功, -1 失败
 */
int funsos_fsetxattr(int fd, const char *name, 
                     const void *value, uint32_t size, int flags);

/*
 * 设置扩展属性（不跟随符号链接）
 * 参数: path - 文件路径; name - 属性名; value - 属性值; size - 属性值大小; flags - 标志
 * 返回: 0 成功, -1 失败
 */
int funsos_lsetxattr(const char *path, const char *name, 
                     const void *value, uint32_t size, int flags);

/*
 * 获取扩展属性
 * 参数: path - 文件路径; name - 属性名; value - 接收属性值的缓冲区; size - 缓冲区大小
 * 返回: 属性值大小, -1 失败
 */
int funsos_getxattr(const char *path, const char *name, void *value, uint32_t size);

/*
 * 获取扩展属性（通过文件描述符）
 * 参数: fd - 文件描述符; name - 属性名; value - 接收属性值的缓冲区; size - 缓冲区大小
 * 返回: 属性值大小, -1 失败
 */
int funsos_fgetxattr(int fd, const char *name, void *value, uint32_t size);

/*
 * 获取扩展属性（不跟随符号链接）
 * 参数: path - 文件路径; name - 属性名; value - 接收属性值的缓冲区; size - 缓冲区大小
 * 返回: 属性值大小, -1 失败
 */
int funsos_lgetxattr(const char *path, const char *name, void *value, uint32_t size);

/*
 * 列出扩展属性名
 * 参数: path - 文件路径; list - 接收属性名列表的缓冲区; size - 缓冲区大小
 * 返回: 属性名列表总长度, -1 失败
 */
int funsos_listxattr(const char *path, char *list, uint32_t size);

/*
 * 移除扩展属性
 * 参数: path - 文件路径; name - 属性名
 * 返回: 0 成功, -1 失败
 */
int funsos_removexattr(const char *path, const char *name);

/* ---- 高级文件操作 ---- */

/*
 * 复制文件
 * 参数: src - 源文件路径; dst - 目标文件路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_copy(const char *src, const char *dst);

/*
 * 移动/重命名文件
 * 参数: oldpath - 原路径; newpath - 新路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_move(const char *oldpath, const char *newpath);

/*
 * 递归复制目录
 * 参数: src - 源目录路径; dst - 目标目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_dir_copy(const char *src, const char *dst);

/*
 * 在目录中查找文件
 * 参数: path - 搜索起始路径; pattern - 文件名匹配模式 (支持 * ? 通配符); recursive - 是否递归
 * 返回: 找到的文件数量, -1 失败
 */
int funsos_file_find(const char *path, const char *pattern, int recursive);

/*
 * 获取文件的真实路径（解析符号链接和相对路径）
 * 参数: path - 文件路径; resolved_path - 接收解析后路径的缓冲区
 * 返回: 解析后的路径指针, NULL 失败
 */
char *funsos_realpath(const char *path, char *resolved_path);

/*
 * 创建临时文件
 * 参数: template - 文件名模板（末尾必须是XXXXXX）
 * 返回: 文件描述符, -1 失败
 */
int funsos_mkstemp(char *template);

/*
 * 创建临时目录
 * 参数: template - 目录名模板（末尾必须是XXXXXX）
 * 返回: 目录路径指针, NULL 失败
 */
char *funsos_mkdtemp(char *template);

/* ---- 文件系统挂载操作 ---- */

/* 挂载标志 */
#define FUNSOS_MS_RDONLY      0x0001   /* 只读挂载 */
#define FUNSOS_MS_NOSUID      0x0002   /* 禁止 setuid/setgid */
#define FUNSOS_MS_NODEV       0x0004   /* 禁止访问设备 */
#define FUNSOS_MS_NOEXEC      0x0008   /* 禁止执行程序 */
#define FUNSOS_MS_SYNCHRONOUS 0x0010   /* 同步写入 */
#define FUNSOS_MS_REMOUNT     0x0020   /* 重新挂载 */
#define FUNSOS_MS_BIND        0x0040   /* 绑定挂载 */
#define FUNSOS_MS_MOVE        0x0080   /* 移动挂载点 */

/*
 * 挂载文件系统
 * 参数: source - 源设备/文件; target - 挂载点; fstype - 文件系统类型; flags - 挂载标志; data - 文件系统特定数据
 * 返回: 0 成功, -1 失败
 */
int funsos_mount(const char *source, const char *target, 
                 const char *fstype, uint32_t flags, const void *data);

/*
 * 卸载文件系统
 * 参数: target - 挂载点路径
 * 返回: 0 成功, -1 失败
 */
int funsos_umount(const char *target);

/*
 * 强制卸载文件系统
 * 参数: target - 挂载点路径
 * 返回: 0 成功, -1 失败
 */
int funsos_umount2(const char *target, int flags);

#endif /* FUNSOS_FS_H */
