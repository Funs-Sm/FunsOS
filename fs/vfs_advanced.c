#include "vfs_advanced.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "timer.h"
#include "../kernel/process.h"
#include "../kernel/sched.h"

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

/* inotify 监视表（最多32个watch）*/
#define MAX_INOTIFY_WATCHES 32

typedef struct inotify_watch {
    int32_t wd;                    /* watch描述符 */
    int32_t fd;                    /* 关联的fd */
    char path[256];                /* 监视路径 */
    uint32_t mask;                 /* 事件掩码 */
    inotify_event_t *event_queue;  /* 事件队列 */
    uint32_t event_count;          /* 事件数量 */
    uint8_t active;                /* 是否激活 */
} inotify_watch_t;

static inotify_watch_t inotify_watches[MAX_INOTIFY_WATCHES];
static int32_t next_watch_id = 1;
static spinlock_t inotify_lock;
static uint8_t inotify_initialized = 0;

/* 初始化高级VFS功能 */
void vfs_advanced_init(void) {
    spinlock_init(&lock_table_lock);
    spinlock_init(&xattr_lock);
    spinlock_init(&quota_lock);
    spinlock_init(&snapshot_lock);
    spinlock_init(&inotify_lock);

    /* 初始化inotify监视表 */
    if (!inotify_initialized) {
        for (int32_t i = 0; i < MAX_INOTIFY_WATCHES; i++) {
            inotify_watches[i].wd = 0;
            inotify_watches[i].fd = -1;
            inotify_watches[i].mask = 0;
            inotify_watches[i].event_queue = NULL;
            inotify_watches[i].event_count = 0;
            inotify_watches[i].active = 0;
        }
        inotify_initialized = 1;
    }
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
    new_lock->pid = sched_get_current() ? sched_get_current()->pid : 0;
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

/* ==================== inotify 文件监视实现 ==================== */

int32_t vfs_inotify_init(void) {
    /* 确保inotify已初始化 */
    if (!inotify_initialized) {
        vfs_advanced_init();
    }

    /* 返回一个简单的fd（简化实现） */
    /* 实际实现应该分配真正的文件描述符 */
    return 0;  /* 返回inotify实例的fd */
}

int32_t vfs_inotify_add_watch(int32_t fd, const char *path, uint32_t mask) {
    if (!path || fd < 0) return -1;

    spinlock_lock(&inotify_lock);

    /* 查找空闲的watch槽位 */
    int32_t slot = -1;
    for (int32_t i = 0; i < MAX_INOTIFY_WATCHES; i++) {
        if (!inotify_watches[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spinlock_unlock(&inotify_lock);
        return -1;  /* 已达到最大watch数量 */
    }

    /* 初始化watch */
    inotify_watches[slot].wd = next_watch_id++;
    inotify_watches[slot].fd = fd;
    strncpy(inotify_watches[slot].path, path, 255);
    inotify_watches[slot].path[255] = '\0';
    inotify_watches[slot].mask = mask;  /* 支持IN_CREATE/IN_DELETE/IN_MODIFY/IN_ACCESS */
    inotify_watches[slot].event_queue = NULL;
    inotify_watches[slot].event_count = 0;
    inotify_watches[slot].active = 1;

    int32_t wd = inotify_watches[slot].wd;
    spinlock_unlock(&inotify_lock);
    return wd;  /* 返回watch描述符 */
}

int32_t vfs_inotify_rm_watch(int32_t fd, int32_t wd) {
    if (wd <= 0 || fd < 0) return -1;

    spinlock_lock(&inotify_lock);

    for (int32_t i = 0; i < MAX_INOTIFY_WATCHES; i++) {
        if (inotify_watches[i].active && inotify_watches[i].wd == wd && inotify_watches[i].fd == fd) {
            /* 释放事件队列 */
            inotify_event_t *event = inotify_watches[i].event_queue;
            while (event) {
                inotify_event_t *next = event->next;
                kfree(event);
                event = next;
            }

            /* 清空watch槽位 */
            inotify_watches[i].active = 0;
            inotify_watches[i].wd = 0;
            inotify_watches[i].fd = -1;
            inotify_watches[i].mask = 0;
            inotify_watches[i].event_queue = NULL;
            inotify_watches[i].event_count = 0;

            spinlock_unlock(&inotify_lock);
            return 0;
        }
    }

    spinlock_unlock(&inotify_lock);
    return -1;  /* 未找到指定的watch */
}

/* ==================== 校验和计算实现 ==================== */

/* CRC32 查找表（简化版） */
static uint32_t crc32_table[256];
static uint8_t crc32_table_initialized = 0;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t length) {
    if (!crc32_table_initialized) {
        crc32_init_table();
    }

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

/* 简化版MD5（仅用于演示，非安全实现） */
static void md5_simple(const uint8_t *data, uint32_t length, uint8_t *hash) {
    /* 这是一个简化的哈希实现，不是真正的MD5 */
    /* 实际生产环境应使用完整的MD5或SHA256实现 */
    uint32_t state[4] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476};

    for (uint32_t i = 0; i < length; i++) {
        state[0] ^= ((uint32_t)data[i] << (8 * (i % 4)));
        state[1] += data[i] * (i + 1);
        state[2] ^= (data[i] << 16) | (data[i] >> 16);
        state[3] += (uint32_t)data[i] << ((i % 4) * 8);
    }

    /* 输出128位（16字节）哈希 */
    for (int i = 0; i < 4; i++) {
        hash[i * 4]     = (state[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (state[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (state[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = state[i] & 0xFF;
    }
}

int32_t vfs_checksum_compute(const char *path, uint32_t algorithm, file_checksum_t *checksum) {
    if (!path || !checksum) return -1;

    /* 打开文件读取数据 */
    file_t *file = NULL;
    if (vfs_open(path, FILE_MODE_READ, &file) != 0 || !file) {
        return -1;
    }

    /* 获取文件大小 */
    inode_t *inode = file->inode;
    if (!inode) {
        vfs_close(file);
        return -1;
    }

    uint32_t file_size = inode->size;
    if (file_size == 0) {
        vfs_close(file);
        return -1;
    }

    /* 分配缓冲区并读取文件内容 */
    uint8_t *buffer = (uint8_t *)kmalloc(file_size);
    if (!buffer) {
        vfs_close(file);
        return -1;
    }

    int32_t read_result = vfs_read(file, buffer, file_size);
    vfs_close(file);

    if (read_result <= 0) {
        kfree(buffer);
        return -1;
    }

    checksum->algorithm = algorithm;
    checksum->timestamp = timer_get_ticks();

    switch (algorithm) {
        case 0:  /* CRC32 */
        case 1: {
            uint32_t crc = crc32_compute(buffer, read_result);
            checksum->hash_len = 4;
            memset(checksum->hash, 0, 64);
            memcpy(checksum->hash, &crc, 4);
            break;
        }
        case 2:  /* MD5简化版 */
        default: {
            md5_simple(buffer, read_result, checksum->hash);
            checksum->hash_len = 16;
            break;
        }
    }

    kfree(buffer);
    return 0;
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
    if (!path || !buf || count == 0) return -1;

    /* 创建临时文件路径 */
    char temp_path[300];
    strncpy(temp_path, path, 280);
    strcat(temp_path, ".tmp~");

    /* 创建并写入临时文件 */
    file_t *temp_file = NULL;
    if (vfs_creat(temp_path, FILE_MODE_WRITE | FILE_MODE_REG) != 0) {
        return -1;
    }

    if (vfs_open(temp_path, FILE_MODE_WRITE, &temp_file) != 0 || !temp_file) {
        vfs_unlink(temp_path);
        return -1;
    }

    int32_t write_result = vfs_write(temp_file, buf, count);
    vfs_close(temp_file);

    if (write_result < 0) {
        vfs_unlink(temp_path);
        return -1;
    }

    /* 原子重命名：将临时文件重命名为目标文件 */
    int32_t rename_result = vfs_rename(temp_path, path);

    if (rename_result != 0) {
        /* 重命名失败，清理临时文件 */
        vfs_unlink(temp_path);
        return -1;
    }

    return count;  /* 返回写入的字节数 */
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
    /* 1. 刷新所有缓存数据到磁盘 */
    extern int32_t cache_flush_all(void);
    cache_flush_all();

    /* 2. 同步所有挂载的文件系统 */
    /* 简化实现：调用全局vfs_sync */
    vfs_sync();

    return 0;
}

int32_t vfs_readahead(file_t *file, uint64_t offset, uint64_t length) {
    if (!file || !file->inode) return -1;

    /* 计算需要预读取的块范围 */
    uint32_t block_size = 4096;  /* 假设块大小为4KB */
    uint64_t start_block = offset / block_size;
    uint64_t end_block = (offset + length + block_size - 1) / block_size;

    /* 预读取后续块到缓存（顺序访问模式） */
    extern int32_t cache_prefetch(uint32_t block_num);

    uint64_t prefetch_count = 0;
    const uint64_t max_prefetch = 8;  /* 最多预读取8个块 */

    for (uint64_t block = start_block; block <= end_block && prefetch_count < max_prefetch; block++) {
        if (cache_prefetch((uint32_t)block) == 0) {
            prefetch_count++;
        }
    }

    return (int32_t)prefetch_count;  /* 返回实际预读取的块数 */
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
