/*
 * FUNSOS 目录操作 API 实现
 * ========================
 * 封装内核 VFS 系统调用，提供目录遍历、创建/删除等操作。
 *
 */

#include "funsos.h"
#include "funsos_dir.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_READDIR   18
#define SYS_CHDIR     19
#define SYS_GETCWD    20
#define SYS_IOCTL     17

/* 目录流内部结构 */
struct funsos_dir {
    int              fd;          /* 目录文件描述符 */
    int              pos;         /* 当前位置 */
    funsos_dirent_t  current;     /* 当前目录项 */
    char             buf[4096];   /* 目录项缓冲区 */
    int              buf_pos;     /* 缓冲区当前位置 */
    int              buf_len;     /* 缓冲区数据长度 */
};

/* 系统调用包装 */
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

/*
 * 打开目录
 */
FUNSOS_DIR *funsos_opendir(const char *name)
{
    if (name == NULL) return NULL;

    FUNSOS_DIR *dir = (FUNSOS_DIR *)funsos_alloc(sizeof(FUNSOS_DIR));
    if (dir == NULL) return NULL;

    /* 使用 O_DIRECTORY 标志打开目录 */
    dir->fd = syscall2(SYS_OPEN, (int)name, FUNSOS_O_RDONLY | FUNSOS_O_DIRECTORY);
    if (dir->fd < 0) {
        funsos_free(dir);
        return NULL;
    }

    dir->pos = 0;
    dir->buf_pos = 0;
    dir->buf_len = 0;

    return dir;
}

/*
 * 关闭目录
 */
int funsos_closedir(FUNSOS_DIR *dirp)
{
    if (dirp == NULL) return -1;

    int ret = syscall1(SYS_CLOSE, dirp->fd);
    funsos_free(dirp);
    return ret;
}

/*
 * 读取目录项
 */
funsos_dirent_t *funsos_readdir(FUNSOS_DIR *dirp)
{
    if (dirp == NULL) return NULL;

    /* 如果缓冲区已读完，重新读取 */
    if (dirp->buf_pos >= dirp->buf_len) {
        int n = syscall3(SYS_READDIR, dirp->fd, (int)dirp->buf, sizeof(dirp->buf));
        if (n <= 0) return NULL;
        dirp->buf_len = n;
        dirp->buf_pos = 0;
    }

    /* 从缓冲区解析一个目录项 */
    funsos_dirent_t *entry = (funsos_dirent_t *)(dirp->buf + dirp->buf_pos);
    if (entry->d_reclen == 0) return NULL;

    dirp->current = *entry;
    dirp->buf_pos += entry->d_reclen;
    dirp->pos++;

    return &dirp->current;
}

/*
 * 重置目录流位置到开头
 */
void funsos_rewinddir(FUNSOS_DIR *dirp)
{
    if (dirp == NULL) return;

    /* 通过 lseek 到 0 来重置 */
    syscall3(SYS_IOCTL, dirp->fd, 0, 0);
    dirp->pos = 0;
    dirp->buf_pos = 0;
    dirp->buf_len = 0;
}

/*
 * 获取目录流当前位置
 */
long funsos_telldir(FUNSOS_DIR *dirp)
{
    if (dirp == NULL) return -1;
    return dirp->pos;
}

/*
 * 设置目录流位置
 */
void funsos_seekdir(FUNSOS_DIR *dirp, long loc)
{
    if (dirp == NULL) return;

    /* 简化实现：从头开始读 loc 个 */
    funsos_rewinddir(dirp);
    for (long i = 0; i < loc; i++) {
        if (funsos_readdir(dirp) == NULL) break;
    }
}

/*
 * 创建目录（可指定权限）
 */
int funsos_mkdir(const char *path, uint32_t mode)
{
    if (path == NULL) return -1;
    /* 使用 ioctl 创建目录 */
    return syscall3(SYS_IOCTL, -1, 2, (int)path);
}

/*
 * 删除空目录
 */
int funsos_rmdir(const char *path)
{
    if (path == NULL) return -1;
    /* 使用 ioctl 删除目录 */
    return syscall3(SYS_IOCTL, -1, 3, (int)path);
}

/*
 * 递归创建目录
 */
int funsos_mkdir_p(const char *path, uint32_t mode)
{
    if (path == NULL || path[0] == '\0') return -1;

    char tmp[512];
    int len = 0;
    while (path[len] && len < 511) {
        tmp[len] = path[len];
        len++;
        if (path[len] == '/' || path[len] == '\0') {
            tmp[len] = '\0';
            if (len > 0 && tmp[0] != '/') {
                /* 尝试创建 */
                funsos_file_mkdir(tmp);
            } else if (len > 1) {
                funsos_file_mkdir(tmp);
            }
        }
    }
    return 0;
}

/*
 * 递归删除目录
 */
int funsos_rmdir_r(const char *path)
{
    if (path == NULL) return -1;

    FUNSOS_DIR *dir = funsos_opendir(path);
    if (dir == NULL) return -1;

    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        int i = 0, j = 0;
        while (path[i]) { fullpath[j++] = path[i++]; }
        fullpath[j++] = '/';
        i = 0;
        while (entry->d_name[i]) { fullpath[j++] = entry->d_name[i++]; }
        fullpath[j] = '\0';

        if (entry->d_type == FUNSOS_DT_DIR) {
            funsos_rmdir_r(fullpath);
        } else {
            funsos_file_remove(fullpath);
        }
    }

    funsos_closedir(dir);
    return funsos_file_rmdir(path);
}

/*
 * 检查目录是否为空
 */
int funsos_dir_empty(const char *path)
{
    if (path == NULL) return -1;

    FUNSOS_DIR *dir = funsos_opendir(path);
    if (dir == NULL) return -1;

    int empty = 1;
    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            empty = 0;
            break;
        }
    }

    funsos_closedir(dir);
    return empty;
}

/*
 * 获取目录大小（递归统计所有文件大小）
 */
int64_t funsos_dir_size(const char *path)
{
    if (path == NULL) return -1;

    int64_t total = 0;
    FUNSOS_DIR *dir = funsos_opendir(path);
    if (dir == NULL) return -1;

    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        int i = 0, j = 0;
        while (path[i]) { fullpath[j++] = path[i++]; }
        fullpath[j++] = '/';
        i = 0;
        while (entry->d_name[i]) { fullpath[j++] = entry->d_name[i++]; }
        fullpath[j] = '\0';

        if (entry->d_type == FUNSOS_DT_DIR) {
            total += funsos_dir_size(fullpath);
        } else {
            total += entry->size;
        }
    }

    funsos_closedir(dir);
    return total;
}

/*
 * 递归遍历目录
 */
int funsos_dir_walk(const char *path, funsos_dir_walk_callback_t callback, void *user_data)
{
    if (path == NULL || callback == NULL) return -1;

    FUNSOS_DIR *dir = funsos_opendir(path);
    if (dir == NULL) return -1;

    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        int i = 0, j = 0;
        while (path[i]) { fullpath[j++] = path[i++]; }
        fullpath[j++] = '/';
        i = 0;
        while (entry->d_name[i]) { fullpath[j++] = entry->d_name[i++]; }
        fullpath[j] = '\0';

        int ret = callback(fullpath, entry->d_name, entry->d_type, user_data);
        if (ret != 0) {
            funsos_closedir(dir);
            return ret;
        }

        if (entry->d_type == FUNSOS_DT_DIR) {
            ret = funsos_dir_walk(fullpath, callback, user_data);
            if (ret != 0) {
                funsos_closedir(dir);
                return ret;
            }
        }
    }

    funsos_closedir(dir);
    return 0;
}

/*
 * 非递归遍历目录（只遍历一层）
 */
int funsos_dir_foreach(const char *path, funsos_dir_walk_callback_t callback, void *user_data)
{
    if (path == NULL || callback == NULL) return -1;

    FUNSOS_DIR *dir = funsos_opendir(path);
    if (dir == NULL) return -1;

    funsos_dirent_t *entry;
    while ((entry = funsos_readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        int ret = callback(path, entry->d_name, entry->d_type, user_data);
        if (ret != 0) {
            funsos_closedir(dir);
            return ret;
        }
    }

    funsos_closedir(dir);
    return 0;
}
