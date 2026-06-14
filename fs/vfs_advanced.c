#include "vfs_advanced.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "timer.h"

/* 全局文件锁表 */
static file_lock_t *global_lock_list = NULL;
static spinlock_t lock_table_lock;

/* 全局扩展属性存储 */
typedef struct xattr_inode_entry {
    uint32_t ino;
    xattr_entry_t *xattrs;
    struct xattr_inode_entry *next;
} xattr_inode_entry_t;

static xattr_inode_entry_t *xattr_table = NULL;
static spinlock_t xattr_lock;

/* 全局配额表 */
static fs_quota_t *quota_table = NULL;
static uint32_t quota_count = 0;
static spinlock_t quota_lock;

/* 全局快照表 */
static fs_snapshot_t *snapshot_list = NULL;
static uint32_t next_snapshot_id = 1;
static spinlock_t snapshot_lock;

/* 初始化高级VFS功能 */
void vfs_advanced_init(void) {
    spinlock_init(&lock_table_lock);
    spinlock_init(&xattr_lock);
    spinlock_init(&quota_lock);
    spinlock_init(&snapshot_lock);
}

/* ==================== 文件锁实现 ==================== */

int32_t vfs_flock(file_t *file, uint32_t type, uint64_t start, uint64_t length) {
    if (!file || !file->inode) return -1;
    if (type != FLOCK_SHARED && type != FLOCK_EXCLUSIVE) return -1;

    spinlock_lock(&lock_table_lock);

    /* 检查冲突 */
    file_lock_t *curr = global_lock_list;
    while (curr) {
        if (curr->type == FLOCK_EXCLUSIVE || type == FLOCK_EXCLUSIVE) {
            /* 检查范围重叠 */
            uint64_t curr_end = curr->start + curr->length;
            uint64_t new_end = start + length;
            if (!(new_end <= curr->start || start >= curr_end)) {
                spinlock_unlock(&lock_table_lock);
                return -1; /* 冲突 */
            }
        }
        curr = curr->next;
    }

    /* 创建新锁 */
    file_lock_t *new_lock = (file_lock_t *)kmalloc(sizeof(file_lock_t));
    if (!new_lock) {
        spinlock_unlock(&lock_table_lock);
        return -1;
    }

    new_lock->type = type;
    new_lock->pid = 0; /* TODO: 获取当前进程PID */
    new_lock->start = start;
    new_lock->length = length;
    new_lock->next = global_lock_list;
    global_lock_list = new_lock;

    spinlock_unlock(&lock_table_lock);
    return 0;
}

int32_t vfs_unflock(file_t *file, uint64_t start, uint64_t length) {
    if (!file) return -1;

    spinlock_lock(&lock_table_lock);

    file_lock_t *prev = NULL;
    file_lock_t *curr = global_lock_list;
    
    while (curr) {
        if (curr->start == start && curr->length == length) {
            if (prev) {
                prev->next = curr->next;
            } else {
                global_lock_list = curr->next;
            }
            kfree(curr);
            spinlock_unlock(&lock_table_lock);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    spinlock_unlock(&lock_table_lock);
    return -1;
}

int32_t vfs_test_lock(file_t *file, uint64_t start, uint64_t length) {
    if (!file) return -1;

    spinlock_lock(&lock_table_lock);

    file_lock_t *curr = global_lock_list;
    while (curr) {
        uint64_t curr_end = curr->start + curr->length;
        uint64_t test_end = start + length;
        if (!(test_end <= curr->start || start >= curr_end)) {
            spinlock_unlock(&lock_table_lock);
            return curr->type; /* 返回冲突锁的类型 */
        }
        curr = curr->next;
    }

    spinlock_unlock(&lock_table_lock);
    return FLOCK_NONE;
}

/* ==================== 扩展属性实现 ==================== */

static xattr_inode_entry_t *find_xattr_inode(uint32_t ino) {
    xattr_inode_entry_t *curr = xattr_table;
    while (curr) {
        if (curr->ino == ino) return curr;
        curr = curr->next;
    }
    return NULL;
}

int32_t vfs_setxattr(const char *path, const char *name, const void *value, uint32_t size, uint32_t flags) {
    if (!path || !name || !value) return -1;
    if (size > XATTR_VALUE_MAX) return -1;

    /* 解析路径获取inode */
    dentry_t *dentry = NULL;
    extern int32_t path_resolve(const char *path, dentry_t **result);
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -1;
    }

    spinlock_lock(&xattr_lock);

    uint32_t ino = dentry->inode->ino;
    xattr_inode_entry_t *inode_entry = find_xattr_inode(ino);
    
    if (!inode_entry) {
        /* 创建新的inode条目 */
        inode_entry = (xattr_inode_entry_t *)kmalloc(sizeof(xattr_inode_entry_t));
        if (!inode_entry) {
            spinlock_unlock(&xattr_lock);
            return -1;
        }
        inode_entry->ino = ino;
        inode_entry->xattrs = NULL;
        inode_entry->next = xattr_table;
        xattr_table = inode_entry;
    }

    /* 查找或创建xattr条目 */
    xattr_entry_t *xattr = inode_entry->xattrs;
    xattr_entry_t *prev = NULL;
    
    while (xattr) {
        if (strcmp(xattr->name, name) == 0) {
            /* 更新现有属性 */
            if (xattr->value) kfree(xattr->value);
            xattr->value = kmalloc(size);
            if (!xattr->value) {
                spinlock_unlock(&xattr_lock);
                return -1;
            }
            memcpy(xattr->value, value, size);
            xattr->value_len = size;
            spinlock_unlock(&xattr_lock);
            return 0;
        }
        prev = xattr;
        xattr = xattr->next;
    }

    /* 创建新属性 */
    xattr_entry_t *new_xattr = (xattr_entry_t *)kmalloc(sizeof(xattr_entry_t));
    if (!new_xattr) {
        spinlock_unlock(&xattr_lock);
        return -1;
    }

    strncpy(new_xattr->name, name, XATTR_NAME_MAX);
    new_xattr->name[XATTR_NAME_MAX] = '\0';
    new_xattr->value = kmalloc(size);
    if (!new_xattr->value) {
        kfree(new_xattr);
        spinlock_unlock(&xattr_lock);
        return -1;
    }
    memcpy(new_xattr->value, value, size);
    new_xattr->value_len = size;
    new_xattr->next = inode_entry->xattrs;
    inode_entry->xattrs = new_xattr;

    spinlock_unlock(&xattr_lock);
    return 0;
}

int32_t vfs_getxattr(const char *path, const char *name, void *value, uint32_t size) {
    if (!path || !name || !value) return -1;

    dentry_t *dentry = NULL;
    extern int32_t path_resolve(const char *path, dentry_t **result);
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -1;
    }

    spinlock_lock(&xattr_lock);

    xattr_inode_entry_t *inode_entry = find_xattr_inode(dentry->inode->ino);
    if (!inode_entry) {
        spinlock_unlock(&xattr_lock);
        return -1;
    }

    xattr_entry_t *xattr = inode_entry->xattrs;
    while (xattr) {
        if (strcmp(xattr->name, name) == 0) {
            uint32_t copy_size = xattr->value_len < size ? xattr->value_len : size;
            memcpy(value, xattr->value, copy_size);
            spinlock_unlock(&xattr_lock);
            return (int32_t)copy_size;
        }
        xattr = xattr->next;
    }

    spinlock_unlock(&xattr_lock);
    return -1;
}

int32_t vfs_listxattr(const char *path, char *list, uint32_t size) {
    if (!path || !list) return -1;

    dentry_t *dentry = NULL;
    extern int32_t path_resolve(const char *path, dentry_t **result);
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -1;
    }

    spinlock_lock(&xattr_lock);

    xattr_inode_entry_t *inode_entry = find_xattr_inode(dentry->inode->ino);
    if (!inode_entry) {
        spinlock_unlock(&xattr_lock);
        return 0;
    }

    uint32_t offset = 0;
    xattr_entry_t *xattr = inode_entry->xattrs;
    
    while (xattr && offset < size) {
        uint32_t name_len = strlen(xattr->name) + 1;
        if (offset + name_len > size) break;
        memcpy(list + offset, xattr->name, name_len);
        offset += name_len;
        xattr = xattr->next;
    }

    spinlock_unlock(&xattr_lock);
    return (int32_t)offset;
}

int32_t vfs_removexattr(const char *path, const char *name) {
    if (!path || !name) return -1;

    dentry_t *dentry = NULL;
    extern int32_t path_resolve(const char *path, dentry_t **result);
    if (path_resolve(path, &dentry) != 0 || !dentry || !dentry->inode) {
        return -1;
    }

    spinlock_lock(&xattr_lock);

    xattr_inode_entry_t *inode_entry = find_xattr_inode(dentry->inode->ino);
    if (!inode_entry) {
        spinlock_unlock(&xattr_lock);
        return -1;
    }

    xattr_entry_t *prev = NULL;
    xattr_entry_t *xattr = inode_entry->xattrs;
    
    while (xattr) {
        if (strcmp(xattr->name, name) == 0) {
            if (prev) {
                prev->next = xattr->next;
            } else {
                inode_entry->xattrs = xattr->next;
            }
            if (xattr->value) kfree(xattr->value);
            kfree(xattr);
            spinlock_unlock(&xattr_lock);
            return 0;
        }
        prev = xattr;
        xattr = xattr->next;
    }

    spinlock_unlock(&xattr_lock);
    return -1;
}

/* ==================== 配额管理实现 ==================== */

int32_t vfs_quota_set(const char *path, fs_quota_t *quota) {
    if (!path || !quota) return -1;

    spinlock_lock(&quota_lock);

    /* 查找现有配额 */
    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].uid == quota->uid) {
            /* 更新现有配额 */
            quota_table[i] = *quota;
            spinlock_unlock(&quota_lock);
            return 0;
        }
    }

    /* 创建新配额 - 简化实现，使用固定大小数组 */
    #define MAX_QUOTAS 64
    if (quota_count >= MAX_QUOTAS) {
        spinlock_unlock(&quota_lock);
        return -1;
    }

    if (!quota_table) {
        quota_table = (fs_quota_t *)kmalloc(sizeof(fs_quota_t) * MAX_QUOTAS);
        if (!quota_table) {
            spinlock_unlock(&quota_lock);
            return -1;
        }
    }

    quota_table[quota_count++] = *quota;
    spinlock_unlock(&quota_lock);
    return 0;
}

int32_t vfs_quota_get(const char *path, uint32_t uid, fs_quota_t *quota) {
    if (!path || !quota) return -1;

    spinlock_lock(&quota_lock);

    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].uid == uid) {
            *quota = quota_table[i];
            spinlock_unlock(&quota_lock);
            return 0;
        }
    }

    spinlock_unlock(&quota_lock);
    return -1;
}

int32_t vfs_quota_check(const char *path, uint32_t uid, uint64_t blocks) {
    if (!path) return -1;

    spinlock_lock(&quota_lock);

    for (uint32_t i = 0; i < quota_count; i++) {
        if (quota_table[i].uid == uid) {
            if (quota_table[i].block_used + blocks > quota_table[i].block_hard) {
                spinlock_unlock(&quota_lock);
                return -1; /* 超过硬限制 */
            }
            spinlock_unlock(&quota_lock);
            return 0;
        }
    }

    spinlock_unlock(&quota_lock);
    return 0; /* 无配额限制 */
}

/* ==================== 快照管理实现 ==================== */

int32_t vfs_snapshot_create(const char *path, const char *name) {
    if (!path || !name) return -1;

    spinlock_lock(&snapshot_lock);

    fs_snapshot_t *snapshot = (fs_snapshot_t *)kmalloc(sizeof(fs_snapshot_t));
    if (!snapshot) {
        spinlock_unlock(&snapshot_lock);
        return -1;
    }

    snapshot->id = next_snapshot_id++;
    strncpy(snapshot->name, name, 255);
    snapshot->name[255] = '\0';
    snapshot->timestamp = timer_get_ticks(); /* 简化：使用timer ticks */
    snapshot->size = 0; /* TODO: 计算实际大小 */
    snapshot->data = NULL; /* TODO: 实现实际数据快照 */
    snapshot->next = snapshot_list;
    snapshot_list = snapshot;

    spinlock_unlock(&snapshot_lock);
    return (int32_t)snapshot->id;
}

int32_t vfs_snapshot_delete(const char *path, uint32_t snapshot_id) {
    if (!path) return -1;

    spinlock_lock(&snapshot_lock);

    fs_snapshot_t *prev = NULL;
    fs_snapshot_t *curr = snapshot_list;

    while (curr) {
        if (curr->id == snapshot_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                snapshot_list = curr->next;
            }
            if (curr->data) kfree(curr->data);
            kfree(curr);
            spinlock_unlock(&snapshot_lock);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    spinlock_unlock(&snapshot_lock);
    return -1;
}

int32_t vfs_snapshot_list(const char *path, fs_snapshot_t **list) {
    if (!path || !list) return -1;

    spinlock_lock(&snapshot_lock);
    *list = snapshot_list;
    
    int32_t count = 0;
    fs_snapshot_t *curr = snapshot_list;
    while (curr) {
        count++;
        curr = curr->next;
    }

    spinlock_unlock(&snapshot_lock);
    return count;
}

int32_t vfs_snapshot_restore(const char *path, uint32_t snapshot_id) {
    if (!path) return -1;

    spinlock_lock(&snapshot_lock);

    fs_snapshot_t *curr = snapshot_list;
    while (curr) {
        if (curr->id == snapshot_id) {
            /* TODO: 实现实际的快照恢复逻辑 */
            spinlock_unlock(&snapshot_lock);
            return 0;
        }
        curr = curr->next;
    }

    spinlock_unlock(&snapshot_lock);
    return -1;
}

/* ==================== 其他高级功能存根实现 ==================== */

int32_t vfs_inotify_init(void) {
    /* TODO: 实现inotify初始化 */
    return 0;
}

int32_t vfs_inotify_add_watch(int32_t fd, const char *path, uint32_t mask) {
    /* TODO: 实现添加监视 */
    (void)fd; (void)path; (void)mask;
    return 0;
}

int32_t vfs_inotify_rm_watch(int32_t fd, int32_t wd) {
    /* TODO: 实现移除监视 */
    (void)fd; (void)wd;
    return 0;
}

int32_t vfs_checksum_compute(const char *path, uint32_t algorithm, file_checksum_t *checksum) {
    /* TODO: 实现校验和计算 */
    (void)path; (void)algorithm; (void)checksum;
    return -1;
}

int32_t vfs_checksum_verify(const char *path, file_checksum_t *checksum) {
    /* TODO: 实现校验和验证 */
    (void)path; (void)checksum;
    return -1;
}

int32_t vfs_compress(const char *path, uint32_t algorithm) {
    /* TODO: 实现文件压缩 */
    (void)path; (void)algorithm;
    return -1;
}

int32_t vfs_decompress(const char *path) {
    /* TODO: 实现文件解压 */
    (void)path;
    return -1;
}

int32_t vfs_get_compression_info(const char *path, file_compression_t *info) {
    /* TODO: 实现获取压缩信息 */
    (void)path; (void)info;
    return -1;
}

int32_t vfs_clone_file(const char *src, const char *dst) {
    /* TODO: 实现文件克隆（CoW） */
    (void)src; (void)dst;
    return -1;
}

int32_t vfs_reflink(const char *src, const char *dst, uint64_t offset, uint64_t length) {
    /* TODO: 实现reflink */
    (void)src; (void)dst; (void)offset; (void)length;
    return -1;
}

int32_t vfs_defrag(const char *path) {
    /* TODO: 实现碎片整理 */
    (void)path;
    return 0;
}

int32_t vfs_get_fragmentation(const char *path, uint32_t *frag_percent) {
    /* TODO: 实现获取碎片率 */
    (void)path;
    if (frag_percent) *frag_percent = 0;
    return 0;
}

int32_t vfs_atomic_write(const char *path, const void *buf, uint32_t count) {
    /* TODO: 实现原子写入 */
    (void)path; (void)buf; (void)count;
    return -1;
}

int32_t vfs_atomic_rename(const char *oldpath, const char *newpath) {
    /* TODO: 实现原子重命名 */
    extern int32_t vfs_rename(const char *oldpath, const char *newpath);
    return vfs_rename(oldpath, newpath);
}

int32_t vfs_version_create(const char *path) {
    /* TODO: 实现版本创建 */
    (void)path;
    return 1; /* 返回版本号 */
}

int32_t vfs_version_list(const char *path, uint32_t *versions, uint32_t max_count) {
    /* TODO: 实现版本列表 */
    (void)path; (void)versions; (void)max_count;
    return 0;
}

int32_t vfs_version_restore(const char *path, uint32_t version) {
    /* TODO: 实现版本恢复 */
    (void)path; (void)version;
    return -1;
}

int32_t vfs_sync_dir(const char *path) {
    /* TODO: 实现目录同步 */
    (void)path;
    return 0;
}

int32_t vfs_sync_all(void) {
    /* TODO: 实现全局同步 */
    return 0;
}

int32_t vfs_readahead(file_t *file, uint64_t offset, uint64_t length) {
    /* TODO: 实现预读取 */
    (void)file; (void)offset; (void)length;
    return 0;
}

int32_t vfs_direct_read(file_t *file, void *buf, uint32_t count) {
    /* TODO: 实现直接读取 */
    if (!file || !file->ops || !file->ops->read) return -1;
    return file->ops->read(file, buf, count);
}

int32_t vfs_direct_write(file_t *file, const void *buf, uint32_t count) {
    /* TODO: 实现直接写入 */
    if (!file || !file->ops || !file->ops->write) return -1;
    return file->ops->write(file, buf, count);
}
