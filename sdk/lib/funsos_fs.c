/*
 * FUNSOS 文件系统高级操作 API 实现
 * ==================================
 * 封装内核 VFS 高级功能，提供文件监控、扩展属性、高级文件操作等。
 *
 */

#include "funsos.h"
#include "funsos_fs.h"
#include "funsos_dir.h"
#include "funsos_stat.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_IOCTL     17

/* inotify 系统调用号 */
#define SYS_INOTIFY_INIT    200
#define SYS_INOTIFY_ADD_WATCH 201
#define SYS_INOTIFY_RM_WATCH  202

/* 系统调用包装 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int syscall4(int num, int a1, int a2, int a3, int a4) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

/* ---- inotify 文件监控 ---- */

/*
 * 初始化文件监控实例
 */
int funsos_inotify_init(void)
{
    return syscall0(SYS_INOTIFY_INIT);
}

/*
 * 添加监控
 */
int funsos_inotify_add_watch(int fd, const char *pathname, uint32_t mask)
{
    return syscall3(SYS_INOTIFY_ADD_WATCH, fd, (int)pathname, (int)mask);
}

/*
 * 移除监控
 */
int funsos_inotify_rm_watch(int fd, int wd)
{
    return syscall2(SYS_INOTIFY_RM_WATCH, fd, wd);
}

/*
 * 读取监控事件
 */
int funsos_inotify_read(int fd, void *buf, uint32_t len)
{
    return syscall3(SYS_READ, fd, (int)buf, (int)len);
}

/* ---- 扩展属性 ---- */

/*
 * 设置扩展属性
 */
int funsos_setxattr(const char *path, const char *name,
                    const void *value, uint32_t size, int flags)
{
    (void)path; (void)name; (void)value; (void)size; (void)flags;
    return -1;
}

/*
 * 设置扩展属性（通过文件描述符）
 */
int funsos_fsetxattr(int fd, const char *name,
                     const void *value, uint32_t size, int flags)
{
    (void)fd; (void)name; (void)value; (void)size; (void)flags;
    return -1;
}

/*
 * 设置扩展属性（不跟随符号链接）
 */
int funsos_lsetxattr(const char *path, const char *name,
                     const void *value, uint32_t size, int flags)
{
    (void)path; (void)name; (void)value; (void)size; (void)flags;
    return -1;
}

/*
 * 获取扩展属性
 */
int funsos_getxattr(const char *path, const char *name, void *value, uint32_t size)
{
    (void)path; (void)name; (void)value; (void)size;
    return -1;
}

/*
 * 获取扩展属性（通过文件描述符）
 */
int funsos_fgetxattr(int fd, const char *name, void *value, uint32_t size)
{
    (void)fd; (void)name; (void)value; (void)size;
    return -1;
}

/*
 * 获取扩展属性（不跟随符号链接）
 */
int funsos_lgetxattr(const char *path, const char *name, void *value, uint32_t size)
{
    (void)path; (void)name; (void)value; (void)size;
    return -1;
}

/*
 * 列出扩展属性名
 */
int funsos_listxattr(const char *path, char *list, uint32_t size)
{
    (void)path; (void)list; (void)size;
    return -1;
}

/*
 * 移除扩展属性
 */
int funsos_removexattr(const char *path, const char *name)
{
    (void)path; (void)name;
    return -1;
}

/* ---- 高级文件操作 ---- */

/*
 * 复制文件
 */
int funsos_file_copy(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL) return -1;

    int sfd = funsos_file_open(src, FUNSOS_O_RDONLY);
    if (sfd < 0) return -1;

    int dfd = funsos_file_open(dst, FUNSOS_O_WRONLY | FUNSOS_O_CREAT | FUNSOS_O_TRUNC);
    if (dfd < 0) {
        funsos_file_close(sfd);
        return -1;
    }

    char buf[4096];
    int n;
    while ((n = funsos_file_read(sfd, buf, sizeof(buf))) > 0) {
        if (funsos_file_write(dfd, buf, n) != n) {
            funsos_file_close(sfd);
            funsos_file_close(dfd);
            return -1;
        }
    }

    funsos_file_close(sfd);
    funsos_file_close(dfd);
    return (n < 0) ? -1 : 0;
}

/*
 * 移动/重命名文件
 */
int funsos_file_move(const char *oldpath, const char *newpath)
{
    if (oldpath == NULL || newpath == NULL) return -1;
    return funsos_file_rename(oldpath, newpath);
}

/*
 * 递归复制目录
 */
int funsos_dir_copy(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL) return -1;

    funsos_file_mkdir(dst);

    FUNSOS_DIR *dir = funsos_opendir(src);
    if (dir == NULL) return -1;

    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char srcpath[512], dstpath[512];

        int i = 0, j = 0;
        while (src[i]) { srcpath[j++] = src[i++]; }
        srcpath[j++] = '/';
        i = 0;
        while (entry->d_name[i]) { srcpath[j++] = entry->d_name[i++]; }
        srcpath[j] = '\0';

        i = 0; j = 0;
        while (dst[i]) { dstpath[j++] = dst[i++]; }
        dstpath[j++] = '/';
        i = 0;
        while (entry->d_name[i]) { dstpath[j++] = entry->d_name[i++]; }
        dstpath[j] = '\0';

        if (entry->d_type == FUNSOS_DT_DIR) {
            funsos_dir_copy(srcpath, dstpath);
        } else {
            funsos_file_copy(srcpath, dstpath);
        }
    }

    funsos_closedir(dir);
    return 0;
}

/*
 * 在目录中查找文件
 */
int funsos_file_find(const char *path, const char *pattern, int recursive)
{
    (void)path; (void)pattern; (void)recursive;
    return 0;
}

/*
 * 获取文件的真实路径
 */
char *funsos_realpath(const char *path, char *resolved_path)
{
    if (path == NULL || resolved_path == NULL) return NULL;
    /* 简化实现：直接复制路径 */
    int i = 0;
    while (path[i] && i < 255) {
        resolved_path[i] = path[i];
        i++;
    }
    resolved_path[i] = '\0';
    return resolved_path;
}

/*
 * 创建临时文件
 */
int funsos_mkstemp(char *template)
{
    if (template == NULL) return -1;
    /* 简化实现：直接打开文件 */
    return funsos_file_open(template, FUNSOS_O_RDWR | FUNSOS_O_CREAT | FUNSOS_O_EXCL);
}

/*
 * 创建临时目录
 */
char *funsos_mkdtemp(char *template)
{
    if (template == NULL) return NULL;
    if (funsos_file_mkdir(template) < 0) return NULL;
    return template;
}

/* ---- 文件系统挂载操作 ---- */

/*
 * 挂载文件系统
 */
int funsos_mount(const char *source, const char *target,
                 const char *fstype, uint32_t flags, const void *data)
{
    (void)source; (void)target; (void)fstype; (void)flags; (void)data;
    return -1;
}

/*
 * 卸载文件系统
 */
int funsos_umount(const char *target)
{
    (void)target;
    return -1;
}

/*
 * 强制卸载文件系统
 */
int funsos_umount2(const char *target, int flags)
{
    (void)target; (void)flags;
    return -1;
}
