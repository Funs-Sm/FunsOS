#include "vfs.h"
#include "vfs_ext.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "spinlock.h"
#include "timer.h"

/* ================================================================ */
/*  全局状态                                                        */
/* ================================================================ */

static vfs_ext_mount_t *g_mounts = NULL;
static vfs_ext_node_t *g_root = NULL;
static vfs_ext_node_t *g_cwd = NULL;
static uint32_t g_next_inode = 1;

static vfs_ext_fd_t g_fd_table[VFS_EXT_MAX_FDS];
static int g_fd_initialized = 0;
static char g_cwd_buf[VFS_EXT_MAX_PATH] = "/";

/* AIO state */
static vfs_ext_aio_request_t g_aio_requests[VFS_EXT_AIO_MAX_REQUESTS];
static uint32_t g_aio_next_id = 1;
static int g_aio_initialized = 0;

/* ================================================================ */
/*  内部辅助函数                                                    */
/* ================================================================ */

static uint32_t alloc_inode(void)
{
    return g_next_inode++;
}

static vfs_ext_node_t *create_node(const char *name, uint32_t type, uint32_t mode)
{
    vfs_ext_node_t *node = (vfs_ext_node_t *)kmalloc(sizeof(vfs_ext_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(vfs_ext_node_t));
    if (name) {
        strncpy(node->path, name, sizeof(node->path) - 1);
    }
    node->inode = alloc_inode();
    node->type = type;
    node->permissions = mode;
    node->owner_uid = 0;
    node->group_gid = 0;
    node->created = 0;
    node->modified = 0;
    node->accessed = 0;
    node->ref_count = 1;
    node->parent = NULL;
    node->children = NULL;
    node->next = NULL;
    node->fs_data = NULL;
    node->size = 0;

    return node;
}

static void free_node(vfs_ext_node_t *node)
{
    if (!node) return;
    /* 递归释放子节点 */
    vfs_ext_node_t *child = node->children;
    while (child) {
        vfs_ext_node_t *next = child->next;
        free_node(child);
        child = next;
    }
    if (node->fs_data) {
        kfree(node->fs_data);
    }
    kfree(node);
}

static vfs_ext_node_t *find_child(vfs_ext_node_t *parent, const char *name)
{
    if (!parent || !name) return NULL;
    vfs_ext_node_t *child = parent->children;
    while (child) {
        if (strcmp(child->path, name) == 0) {
            return child;
        }
        child = child->next;
    }
    return NULL;
}

static void add_child(vfs_ext_node_t *parent, vfs_ext_node_t *child)
{
    if (!parent || !child) return;
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
}

static void remove_child(vfs_ext_node_t *parent, vfs_ext_node_t *child)
{
    if (!parent || !child) return;
    if (parent->children == child) {
        parent->children = child->next;
    } else {
        vfs_ext_node_t *prev = parent->children;
        while (prev && prev->next != child) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = child->next;
        }
    }
    child->parent = NULL;
    child->next = NULL;
}

/* 从路径中提取父目录路径和文件名 */
static void split_path(const char *path, char *dir_out, int dir_size, char *name_out, int name_size)
{
    if (!path || !dir_out || !name_out) return;
    int len = strlen(path);

    /* 跳过末尾的 '/' */
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    if (last_slash <= 0) {
        /* 根目录或 /name */
        if (dir_out) {
            strncpy(dir_out, "/", dir_size);
        }
        if (name_out) {
            strncpy(name_out, path + 1, name_size);
        }
    } else {
        if (dir_out) {
            int copy = last_slash;
            if (copy > dir_size - 1) copy = dir_size - 1;
            memcpy(dir_out, path, copy);
            dir_out[copy] = '\0';
            if (copy == 0) {
                dir_out[0] = '/';
                dir_out[1] = '\0';
            }
        }
        if (name_out) {
            strncpy(name_out, path + last_slash + 1, name_size);
        }
    }
}

/* 规范化路径（去除 .. 和 . 以及多余斜杠）*/
static void normalize_path(const char *input, char *output, int out_size)
{
    if (!input || !output) return;
    char stack[VFS_EXT_MAX_PATH][256];
    int depth = 0;
    char temp[VFS_EXT_MAX_PATH];
    char *saveptr;
    int i, len;

    strncpy(temp, input, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    if (temp[0] == '/') {
        /* 绝对路径，根目录入栈 */
        strncpy(stack[0], "/", 256);
        depth = 1;
    }

    char *token = strtok_r(temp, "/", &saveptr);
    while (token) {
        if (strcmp(token, ".") == 0) {
            /* 忽略 */
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) {
                depth--;
            }
            if (depth == 0) {
                strncpy(stack[0], "/", 256);
                depth = 1;
            }
        } else if (token[0] != '\0') {
            if (depth >= VFS_EXT_MAX_PATH) break;
            strncpy(stack[depth], token, 256);
            depth++;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    /* 组装输出 */
    output[0] = '\0';
    if (depth == 0) {
        strncpy(output, "/", out_size);
        return;
    }

    if (depth == 1 && strcmp(stack[0], "/") == 0) {
        strncpy(output, "/", out_size);
        return;
    }

    for (i = 0; i < depth; i++) {
        len = strlen(output);
        if (len > 0 && output[len - 1] != '/') {
            strncat(output, "/", out_size - strlen(output) - 1);
        }
        strncat(output, stack[i], out_size - strlen(output) - 1);
    }
}

/* ================================================================ */
/*  初始化                                                          */
/* ================================================================ */

void vfs_ext_init(void)
{
    int i;

    klog_info("vfs_ext: initializing extended VFS...");

    /* 创建根节点 */
    g_root = create_node("/", VFS_EXT_TYPE_DIR, 0755);
    if (!g_root) {
        klog_err("vfs_ext: failed to create root node");
        return;
    }
    g_root->ref_count = 9999; /* 根节点永不释放 */
    g_cwd = g_root;

    /* 初始化文件描述符表 */
    memset(g_fd_table, 0, sizeof(g_fd_table));
    for (i = 0; i < VFS_EXT_MAX_FDS; i++) {
        g_fd_table[i].fd = i;
        g_fd_table[i].in_use = 0;
    }
    /* 保留 0, 1, 2 为标准输入/输出/错误 */
    g_fd_table[0].in_use = 1;
    g_fd_table[1].in_use = 1;
    g_fd_table[2].in_use = 1;
    g_fd_initialized = 1;

    /* CWD 缓冲区 */
    strncpy(g_cwd_buf, "/", VFS_EXT_MAX_PATH);

    klog_info("vfs_ext: initialized, root inode=%u", g_root->inode);
}

/* ================================================================ */
/*  挂载/卸载                                                       */
/* ================================================================ */

int vfs_ext_mount(const char *device, const char *mount_point, const char *fs_type, uint32_t flags)
{
    vfs_ext_mount_t *mnt;
    vfs_ext_node_t *mp_node;

    if (!mount_point || !fs_type) return -1;

    klog_info("vfs_ext: mounting %s at %s (type=%s)", device ? device : "none", mount_point, fs_type);

    /* 检查挂载点是否存在 */
    mp_node = vfs_ext_resolve(mount_point);
    if (!mp_node) {
        /* 创建挂载点 */
        vfs_ext_mkdir(mount_point, 0755);
        mp_node = vfs_ext_resolve(mount_point);
        if (!mp_node) {
            klog_err("vfs_ext: mount point %s not found", mount_point);
            return -1;
        }
    }

    /* 检查是否已挂载 */
    mnt = g_mounts;
    while (mnt) {
        if (strcmp(mnt->mount_point, mount_point) == 0) {
            klog_err("vfs_ext: %s already mounted", mount_point);
            return -1;
        }
        mnt = mnt->next;
    }

    mnt = (vfs_ext_mount_t *)kmalloc(sizeof(vfs_ext_mount_t));
    if (!mnt) return -1;

    memset(mnt, 0, sizeof(vfs_ext_mount_t));
    strncpy(mnt->mount_point, mount_point, sizeof(mnt->mount_point) - 1);
    if (device) {
        strncpy(mnt->device, device, sizeof(mnt->device) - 1);
    }
    strncpy(mnt->fs_type, fs_type, sizeof(mnt->fs_type) - 1);
    mnt->flags = flags;
    mnt->root = mp_node;
    mnt->next = g_mounts;
    g_mounts = mnt;

    klog_info("vfs_ext: mounted %s successfully", mount_point);
    return 0;
}

int vfs_ext_unmount(const char *mount_point)
{
    vfs_ext_mount_t *prev = NULL;
    vfs_ext_mount_t *mnt = g_mounts;

    while (mnt) {
        if (strcmp(mnt->mount_point, mount_point) == 0) {
            if (prev) {
                prev->next = mnt->next;
            } else {
                g_mounts = mnt->next;
            }
            kfree(mnt);
            klog_info("vfs_ext: unmounted %s", mount_point);
            return 0;
        }
        prev = mnt;
        mnt = mnt->next;
    }

    klog_err("vfs_ext: %s not mounted", mount_point);
    return -1;
}

/* ================================================================ */
/*  路径解析                                                        */
/* ================================================================ */

vfs_ext_node_t *vfs_ext_resolve(const char *path)
{
    char normalized[VFS_EXT_MAX_PATH];
    char token[256];
    char temp[VFS_EXT_MAX_PATH];
    vfs_ext_node_t *current;
    char *saveptr;
    char *tok;

    if (!path || path[0] == '\0') return NULL;

    normalize_path(path, normalized, sizeof(normalized));

    /* 判断绝对/相对路径 */
    if (normalized[0] == '/') {
        current = g_root;
    } else {
        current = g_cwd;
    }

    if (strcmp(normalized, "/") == 0) {
        return current;
    }

    strncpy(temp, normalized, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    /* 跳过开头的 '/' */
    tok = strtok_r(temp, "/", &saveptr);
    while (tok && current) {
        current = find_child(current, tok);
        tok = strtok_r(NULL, "/", &saveptr);
    }

    return current;
}

/* ================================================================ */
/*  目录操作                                                        */
/* ================================================================ */

int vfs_ext_mkdir(const char *path, uint32_t mode)
{
    char dir_path[VFS_EXT_MAX_PATH];
    char name[256];
    vfs_ext_node_t *parent;

    if (!path || path[0] == '\0') return -1;

    split_path(path, dir_path, sizeof(dir_path), name, sizeof(name));

    if (name[0] == '\0') return -1;

    parent = vfs_ext_resolve(dir_path);
    if (!parent) {
        klog_err("vfs_ext: mkdir parent %s not found", dir_path);
        return -1;
    }

    if (parent->type != VFS_EXT_TYPE_DIR) {
        klog_err("vfs_ext: mkdir parent %s is not a directory", dir_path);
        return -1;
    }

    if (find_child(parent, name)) {
        klog_err("vfs_ext: mkdir %s already exists", name);
        return -1;
    }

    vfs_ext_node_t *node = create_node(name, VFS_EXT_TYPE_DIR, mode);
    if (!node) return -1;

    add_child(parent, node);
    klog_info("vfs_ext: mkdir %s/%s ok", dir_path, name);
    return 0;
}

int vfs_ext_rmdir(const char *path)
{
    char dir_path[VFS_EXT_MAX_PATH];
    char name[256];
    vfs_ext_node_t *parent;
    vfs_ext_node_t *child;

    if (!path || path[0] == '\0') return -1;

    /* 不允许删除根目录 */
    if (strcmp(path, "/") == 0) return -1;

    split_path(path, dir_path, sizeof(dir_path), name, sizeof(name));

    if (name[0] == '\0') return -1;

    parent = vfs_ext_resolve(dir_path);
    if (!parent) return -1;

    child = find_child(parent, name);
    if (!child) {
        klog_err("vfs_ext: rmdir %s not found", path);
        return -1;
    }

    if (child->type != VFS_EXT_TYPE_DIR) {
        klog_err("vfs_ext: rmdir %s is not a directory", path);
        return -1;
    }

    if (child->children) {
        klog_err("vfs_ext: rmdir %s not empty", path);
        return -1;
    }

    remove_child(parent, child);
    free_node(child);
    klog_info("vfs_ext: rmdir %s ok", path);
    return 0;
}

int vfs_ext_readdir(const char *path, void *buf, int max_entries)
{
    vfs_ext_node_t *dir;
    vfs_ext_node_t *child;
    vfs_ext_dirent_t *entries;
    int count;

    if (!path || !buf || max_entries <= 0) return -1;

    dir = vfs_ext_resolve(path);
    if (!dir) return -1;

    if (dir->type != VFS_EXT_TYPE_DIR) return -1;

    entries = (vfs_ext_dirent_t *)buf;
    count = 0;
    child = dir->children;

    while (child && count < max_entries) {
        entries[count].d_ino = child->inode;
        strncpy(entries[count].d_name, child->path, sizeof(entries[count].d_name) - 1);
        entries[count].d_type = child->type;
        count++;
        child = child->next;
    }

    return count;
}

/* ================================================================ */
/*  文件操作                                                        */
/* ================================================================ */

int vfs_ext_create(const char *path, uint32_t mode)
{
    char dir_path[VFS_EXT_MAX_PATH];
    char name[256];
    vfs_ext_node_t *parent;

    if (!path || path[0] == '\0') return -1;

    split_path(path, dir_path, sizeof(dir_path), name, sizeof(name));

    if (name[0] == '\0') return -1;

    parent = vfs_ext_resolve(dir_path);
    if (!parent) {
        klog_err("vfs_ext: create parent %s not found", dir_path);
        return -1;
    }

    if (parent->type != VFS_EXT_TYPE_DIR) return -1;

    if (find_child(parent, name)) {
        /* 文件已存在，截断 */
        vfs_ext_node_t *existing = find_child(parent, name);
        existing->size = 0;
        if (existing->fs_data) {
            kfree(existing->fs_data);
            existing->fs_data = NULL;
        }
        return 0;
    }

    vfs_ext_node_t *node = create_node(name, VFS_EXT_TYPE_FILE, mode);
    if (!node) return -1;

    add_child(parent, node);
    klog_info("vfs_ext: create %s ok", path);
    return 0;
}

int vfs_ext_remove(const char *path)
{
    char dir_path[VFS_EXT_MAX_PATH];
    char name[256];
    vfs_ext_node_t *parent;
    vfs_ext_node_t *child;

    if (!path || path[0] == '\0') return -1;

    split_path(path, dir_path, sizeof(dir_path), name, sizeof(name));

    if (name[0] == '\0') return -1;

    parent = vfs_ext_resolve(dir_path);
    if (!parent) return -1;

    child = find_child(parent, name);
    if (!child) {
        klog_err("vfs_ext: remove %s not found", path);
        return -1;
    }

    if (child->type == VFS_EXT_TYPE_DIR && child->children) {
        klog_err("vfs_ext: remove %s is a non-empty directory", path);
        return -1;
    }

    remove_child(parent, child);
    free_node(child);
    klog_info("vfs_ext: remove %s ok", path);
    return 0;
}

int vfs_ext_rename(const char *old_path, const char *new_path)
{
    char old_dir[VFS_EXT_MAX_PATH], old_name[256];
    char new_dir[VFS_EXT_MAX_PATH], new_name[256];
    vfs_ext_node_t *old_parent, *new_parent, *node;

    if (!old_path || !new_path) return -1;

    split_path(old_path, old_dir, sizeof(old_dir), old_name, sizeof(old_name));
    split_path(new_path, new_dir, sizeof(new_dir), new_name, sizeof(new_name));

    if (old_name[0] == '\0' || new_name[0] == '\0') return -1;

    old_parent = vfs_ext_resolve(old_dir);
    if (!old_parent) return -1;

    node = find_child(old_parent, old_name);
    if (!node) return -1;

    new_parent = vfs_ext_resolve(new_dir);
    if (!new_parent) return -1;

    if (find_child(new_parent, new_name)) {
        klog_err("vfs_ext: rename target %s exists", new_name);
        return -1;
    }

    /* 从旧父节点移除，加入新父节点 */
    remove_child(old_parent, node);
    strncpy(node->path, new_name, sizeof(node->path) - 1);
    add_child(new_parent, node);

    klog_info("vfs_ext: rename %s -> %s ok", old_path, new_path);
    return 0;
}

int vfs_ext_truncate(const char *path, uint32_t size)
{
    vfs_ext_node_t *node;

    if (!path) return -1;

    node = vfs_ext_resolve(path);
    if (!node) return -1;

    if (size < node->size) {
        /* 缩小：数据仍然保留，只改 size */
        node->size = size;
    } else if (size > node->size) {
        /* 扩大：重新分配 */
        void *new_data = kmalloc(size);
        if (!new_data) return -1;
        memset(new_data, 0, size);
        if (node->fs_data) {
            memcpy(new_data, node->fs_data, node->size);
            kfree(node->fs_data);
        }
        node->fs_data = new_data;
        node->size = size;
    }

    return 0;
}

int vfs_ext_stat(const char *path, void *stat_buf)
{
    vfs_ext_stat_t *st;
    vfs_ext_node_t *node;

    if (!path || !stat_buf) return -1;

    node = vfs_ext_resolve(path);
    if (!node) return -1;

    st = (vfs_ext_stat_t *)stat_buf;
    st->st_ino = node->inode;
    st->st_mode = node->permissions;
    st->st_size = node->size;
    st->st_uid = node->owner_uid;
    st->st_gid = node->group_gid;
    st->st_atime = node->accessed;
    st->st_mtime = node->modified;
    st->st_ctime = node->created;
    st->st_blksize = 512;
    st->st_blocks = (node->size + 511) / 512;

    return 0;
}

/* ================================================================ */
/*  符号链接                                                        */
/* ================================================================ */

int vfs_ext_symlink(const char *target, const char *link_path)
{
    char dir_path[VFS_EXT_MAX_PATH];
    char name[256];
    vfs_ext_node_t *parent;

    if (!target || !link_path) return -1;

    split_path(link_path, dir_path, sizeof(dir_path), name, sizeof(name));

    if (name[0] == '\0') return -1;

    parent = vfs_ext_resolve(dir_path);
    if (!parent) return -1;

    if (find_child(parent, name)) return -1;

    vfs_ext_node_t *node = create_node(name, VFS_EXT_TYPE_SYMLINK, 0777);
    if (!node) return -1;

    /* 将 target 存入 fs_data */
    int target_len = strlen(target) + 1;
    node->fs_data = kmalloc(target_len);
    if (!node->fs_data) {
        kfree(node);
        return -1;
    }
    memcpy(node->fs_data, target, target_len);
    node->size = target_len;

    add_child(parent, node);
    return 0;
}

int vfs_ext_readlink(const char *path, char *buf, int bufsize)
{
    vfs_ext_node_t *node;

    if (!path || !buf || bufsize <= 0) return -1;

    node = vfs_ext_resolve(path);
    if (!node) return -1;

    if (node->type != VFS_EXT_TYPE_SYMLINK) return -1;

    if (node->fs_data) {
        strncpy(buf, (const char *)node->fs_data, bufsize - 1);
        buf[bufsize - 1] = '\0';
        return 0;
    }
    return -1;
}

/* ================================================================ */
/*  权限                                                            */
/* ================================================================ */

int vfs_ext_chmod(const char *path, uint32_t mode)
{
    vfs_ext_node_t *node;

    if (!path) return -1;

    node = vfs_ext_resolve(path);
    if (!node) return -1;

    node->permissions = mode;
    return 0;
}

int vfs_ext_chown(const char *path, uint32_t uid, uint32_t gid)
{
    vfs_ext_node_t *node;

    if (!path) return -1;

    node = vfs_ext_resolve(path);
    if (!node) return -1;

    node->owner_uid = uid;
    node->group_gid = gid;
    return 0;
}

/* ================================================================ */
/*  路径操作                                                        */
/* ================================================================ */

int vfs_ext_getcwd(char *buf, int bufsize)
{
    if (!buf || bufsize <= 0) return -1;
    strncpy(buf, g_cwd_buf, bufsize - 1);
    buf[bufsize - 1] = '\0';
    return 0;
}

int vfs_ext_chdir(const char *path)
{
    vfs_ext_node_t *node;
    char normalized[VFS_EXT_MAX_PATH];

    if (!path) return -1;

    node = vfs_ext_resolve(path);
    if (!node) {
        klog_err("vfs_ext: chdir %s not found", path);
        return -1;
    }

    if (node->type != VFS_EXT_TYPE_DIR) {
        klog_err("vfs_ext: chdir %s is not a directory", path);
        return -1;
    }

    g_cwd = node;
    normalize_path(path, normalized, sizeof(normalized));
    strncpy(g_cwd_buf, normalized, VFS_EXT_MAX_PATH - 1);
    g_cwd_buf[VFS_EXT_MAX_PATH - 1] = '\0';

    return 0;
}

/* ================================================================ */
/*  文件描述符表                                                    */
/* ================================================================ */

static int fd_alloc(void)
{
    int i;
    for (i = 3; i < VFS_EXT_MAX_FDS; i++) {
        if (!g_fd_table[i].in_use) {
            g_fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

int vfs_ext_open(const char *path, int flags, int mode)
{
    vfs_ext_node_t *node;
    int fd;

    if (!path) return -1;

    /* 确保 fd 表已初始化 */
    if (!g_fd_initialized) {
        memset(g_fd_table, 0, sizeof(g_fd_table));
        g_fd_table[0].in_use = 1;
        g_fd_table[1].in_use = 1;
        g_fd_table[2].in_use = 1;
        g_fd_initialized = 1;
    }

    node = vfs_ext_resolve(path);
    if (!node) {
        /* 如果指定了 O_CREAT，则创建 */
        if (flags & VFS_EXT_O_CREAT) {
            if (vfs_ext_create(path, (uint32_t)mode) != 0) {
                return -1;
            }
            node = vfs_ext_resolve(path);
            if (!node) return -1;
        } else {
            return -1;
        }
    }

    fd = fd_alloc();
    if (fd < 0) return -1;

    g_fd_table[fd].fd = fd;
    g_fd_table[fd].node = node;
    g_fd_table[fd].flags = (uint32_t)flags;

    /* 如果是截断模式，清空文件 */
    if (flags & VFS_EXT_O_TRUNC) {
        if (node->fs_data) {
            kfree(node->fs_data);
            node->fs_data = NULL;
        }
        node->size = 0;
    }

    /* 如果是追加模式，定位到末尾 */
    if (flags & VFS_EXT_O_APPEND) {
        g_fd_table[fd].offset = node->size;
    } else {
        g_fd_table[fd].offset = 0;
    }

    node->ref_count++;
    return fd;
}

int vfs_ext_close(int fd)
{
    if (fd < 0 || fd >= VFS_EXT_MAX_FDS) return -1;
    if (!g_fd_table[fd].in_use) return -1;

    if (g_fd_table[fd].node) {
        g_fd_table[fd].node->ref_count--;
    }

    memset(&g_fd_table[fd], 0, sizeof(vfs_ext_fd_t));
    g_fd_table[fd].fd = fd;
    g_fd_table[fd].in_use = 0;

    return 0;
}

int vfs_ext_read(int fd, void *buf, int count)
{
    vfs_ext_node_t *node;
    uint32_t to_read;

    if (fd < 0 || fd >= VFS_EXT_MAX_FDS || !buf || count <= 0) return -1;
    if (!g_fd_table[fd].in_use) return -1;

    node = g_fd_table[fd].node;
    if (!node) return -1;

    /* 检查读权限 */
    if (!(g_fd_table[fd].flags & (VFS_EXT_O_RDONLY | VFS_EXT_O_RDWR))) return -1;

    if (g_fd_table[fd].offset >= node->size) return 0;

    to_read = (uint32_t)count;
    if (g_fd_table[fd].offset + to_read > node->size) {
        to_read = node->size - g_fd_table[fd].offset;
    }

    if (node->fs_data && to_read > 0) {
        memcpy(buf, (uint8_t *)node->fs_data + g_fd_table[fd].offset, to_read);
    }

    g_fd_table[fd].offset += to_read;
    return (int)to_read;
}

int vfs_ext_write(int fd, const void *buf, int count)
{
    vfs_ext_node_t *node;
    uint32_t new_size;

    if (fd < 0 || fd >= VFS_EXT_MAX_FDS || !buf || count <= 0) return -1;
    if (!g_fd_table[fd].in_use) return -1;

    node = g_fd_table[fd].node;
    if (!node) return -1;

    /* 检查写权限 */
    if (!(g_fd_table[fd].flags & (VFS_EXT_O_WRONLY | VFS_EXT_O_RDWR))) return -1;

    new_size = g_fd_table[fd].offset + (uint32_t)count;
    if (new_size > node->size) {
        /* 扩展缓冲区 */
        void *new_data = kmalloc(new_size);
        if (!new_data) return -1;
        memset(new_data, 0, new_size);
        if (node->fs_data) {
            memcpy(new_data, node->fs_data, node->size);
            kfree(node->fs_data);
        }
        node->fs_data = new_data;
        node->size = new_size;
    }

    memcpy((uint8_t *)node->fs_data + g_fd_table[fd].offset, buf, count);
    g_fd_table[fd].offset += (uint32_t)count;

    return count;
}

int vfs_ext_seek(int fd, int offset, int whence)
{
    vfs_ext_node_t *node;
    int32_t new_offset;

    if (fd < 0 || fd >= VFS_EXT_MAX_FDS) return -1;
    if (!g_fd_table[fd].in_use) return -1;

    node = g_fd_table[fd].node;
    if (!node) return -1;

    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = (int32_t)g_fd_table[fd].offset + offset;
        break;
    case SEEK_END:
        new_offset = (int32_t)node->size + offset;
        break;
    default:
        return -1;
    }

    if (new_offset < 0) new_offset = 0;
    g_fd_table[fd].offset = (uint32_t)new_offset;

    return new_offset;
}

int vfs_ext_dup(int old_fd)
{
    int new_fd;

    if (old_fd < 0 || old_fd >= VFS_EXT_MAX_FDS) return -1;
    if (!g_fd_table[old_fd].in_use) return -1;

    new_fd = fd_alloc();
    if (new_fd < 0) return -1;

    memcpy(&g_fd_table[new_fd], &g_fd_table[old_fd], sizeof(vfs_ext_fd_t));
    g_fd_table[new_fd].fd = new_fd;

    if (g_fd_table[new_fd].node) {
        g_fd_table[new_fd].node->ref_count++;
    }

    return new_fd;
}

int vfs_ext_dup2(int old_fd, int new_fd)
{
    if (old_fd < 0 || old_fd >= VFS_EXT_MAX_FDS) return -1;
    if (new_fd < 0 || new_fd >= VFS_EXT_MAX_FDS) return -1;
    if (!g_fd_table[old_fd].in_use) return -1;

    if (old_fd == new_fd) return new_fd;

    /* 如果 new_fd 已打开，先关闭 */
    if (g_fd_table[new_fd].in_use) {
        vfs_ext_close(new_fd);
    }

    memcpy(&g_fd_table[new_fd], &g_fd_table[old_fd], sizeof(vfs_ext_fd_t));
    g_fd_table[new_fd].fd = new_fd;

    if (g_fd_table[new_fd].node) {
        g_fd_table[new_fd].node->ref_count++;
    }

    return new_fd;
}

/* ================================================================ */
/*  磁盘配额                                                        */
/* ================================================================ */

/* 全局配额表（简单实现，最多 64 条） */
#define VFS_EXT_MAX_QUOTAS 64

typedef struct {
    char path[256];
    uint32_t uid;
    uint32_t soft_limit;
    uint32_t hard_limit;
    uint32_t used;
} quota_entry_t;

static quota_entry_t g_quotas[VFS_EXT_MAX_QUOTAS];
static int g_quota_count = 0;

int vfs_ext_set_quota(const char *path, uint32_t uid, uint32_t soft_limit, uint32_t hard_limit)
{
    int i;

    if (!path) return -1;

    /* 查找已有条目 */
    for (i = 0; i < g_quota_count; i++) {
        if (strcmp(g_quotas[i].path, path) == 0 && g_quotas[i].uid == uid) {
            g_quotas[i].soft_limit = soft_limit;
            g_quotas[i].hard_limit = hard_limit;
            return 0;
        }
    }

    /* 新条目 */
    if (g_quota_count >= VFS_EXT_MAX_QUOTAS) return -1;

    strncpy(g_quotas[g_quota_count].path, path, sizeof(g_quotas[0].path) - 1);
    g_quotas[g_quota_count].uid = uid;
    g_quotas[g_quota_count].soft_limit = soft_limit;
    g_quotas[g_quota_count].hard_limit = hard_limit;
    g_quotas[g_quota_count].used = 0;
    g_quota_count++;

    return 0;
}

int vfs_ext_get_quota(const char *path, uint32_t uid, uint32_t *used, uint32_t *soft, uint32_t *hard)
{
    int i;

    if (!path || !used || !soft || !hard) return -1;

    for (i = 0; i < g_quota_count; i++) {
        if (strcmp(g_quotas[i].path, path) == 0 && g_quotas[i].uid == uid) {
            *used = g_quotas[i].used;
            *soft = g_quotas[i].soft_limit;
            *hard = g_quotas[i].hard_limit;
            return 0;
        }
    }

    return -1;
}

/* ================================================================ */
/*  1) Journal/Transaction 支持                                       */
/* ================================================================ */

static vfs_ext_journal_t g_journal;
static int g_journal_in_transaction = 0;

int vfs_ext_journal_init(void)
{
    memset(&g_journal, 0, sizeof(g_journal));
    g_journal.entry_count = 0;
    g_journal.next_entry_id = 1;
    g_journal.active = 1;
    g_journal_in_transaction = 0;
    klog_info("vfs_ext: journal initialized");
    return 0;
}

int vfs_ext_journal_begin(void)
{
    if (g_journal_in_transaction) {
        klog_err("vfs_ext: journal transaction already in progress");
        return -1;
    }
    g_journal_in_transaction = 1;
    g_journal.entry_count = 0;
    klog_info("vfs_ext: journal transaction begin");
    return 0;
}

int vfs_ext_journal_log(uint32_t op, const char *path, const char *target, uint32_t inode, uint32_t mode)
{
    vfs_ext_journal_entry_t *entry;
    if (!g_journal_in_transaction) {
        klog_err("vfs_ext: journal log called outside transaction");
        return -1;
    }
    if (g_journal.entry_count >= VFS_EXT_JOURNAL_MAX_ENTRIES) {
        klog_err("vfs_ext: journal full");
        return -1;
    }
    entry = &g_journal.entries[g_journal.entry_count];
    memset(entry, 0, sizeof(*entry));
    entry->entry_id = g_journal.next_entry_id++;
    entry->operation = op;
    entry->state = VFS_EXT_JOURNAL_STATE_PENDING;
    entry->timestamp = 0;
    if (path) strncpy(entry->path, path, sizeof(entry->path) - 1);
    if (target) strncpy(entry->target_path, target, sizeof(entry->target_path) - 1);
    entry->inode = inode;
    entry->mode = mode;
    g_journal.entry_count++;
    return 0;
}

int vfs_ext_journal_commit(void)
{
    uint32_t i;
    if (!g_journal_in_transaction) {
        klog_err("vfs_ext: journal commit called outside transaction");
        return -1;
    }
    for (i = 0; i < g_journal.entry_count; i++) {
        g_journal.entries[i].state = VFS_EXT_JOURNAL_STATE_COMMIT;
    }
    g_journal_in_transaction = 0;
    klog_info("vfs_ext: journal transaction committed (%u entries)", g_journal.entry_count);
    return 0;
}

int vfs_ext_journal_rollback(void)
{
    uint32_t i;
    if (!g_journal_in_transaction) {
        klog_err("vfs_ext: journal rollback called outside transaction");
        return -1;
    }
    for (i = 0; i < g_journal.entry_count; i++) {
        g_journal.entries[i].state = VFS_EXT_JOURNAL_STATE_ROLLBACK;
    }
    /* 逆序回滚：按相反顺序撤销操作 */
    for (i = g_journal.entry_count; i > 0; i--) {
        vfs_ext_journal_entry_t *entry = &g_journal.entries[i - 1];
        switch (entry->operation) {
        case VFS_EXT_JOURNAL_OP_CREATE:
            vfs_ext_remove(entry->path);
            break;
        case VFS_EXT_JOURNAL_OP_MKDIR:
            vfs_ext_rmdir(entry->path);
            break;
        case VFS_EXT_JOURNAL_OP_RENAME:
            if (entry->target_path[0] && entry->path[0]) {
                vfs_ext_rename(entry->target_path, entry->path);
            }
            break;
        case VFS_EXT_JOURNAL_OP_REMOVE:
            klog_err("vfs_ext: journal rollback cannot undo remove of %s", entry->path);
            break;
        case VFS_EXT_JOURNAL_OP_TRUNCATE:
            if (entry->old_size > 0) {
                vfs_ext_truncate(entry->path, entry->old_size);
            }
            break;
        default:
            break;
        }
    }
    g_journal_in_transaction = 0;
    klog_info("vfs_ext: journal transaction rolled back (%u entries)", g_journal.entry_count);
    return 0;
}

int vfs_ext_journal_replay(void)
{
    uint32_t i, committed = 0;
    klog_info("vfs_ext: journal replay started");
    for (i = 0; i < g_journal.entry_count; i++) {
        vfs_ext_journal_entry_t *entry = &g_journal.entries[i];
        if (entry->state == VFS_EXT_JOURNAL_STATE_PENDING) {
            klog_info("vfs_ext: journal replay skipping pending entry %u", entry->entry_id);
            continue;
        }
        if (entry->state == VFS_EXT_JOURNAL_STATE_COMMIT) {
            committed++;
        }
    }
    g_journal.entry_count = 0;
    klog_info("vfs_ext: journal replay completed (%u committed entries)", committed);
    return (int)committed;
}

/* ================================================================ */
/*  2) File Locking (FLOCK)                                          */
/* ================================================================ */

static vfs_ext_flock_t g_flocks[VFS_EXT_MAX_LOCKS];
static uint32_t g_flock_next_id = 1;
static int g_flock_initialized = 0;

int vfs_ext_flock_init(void)
{
    memset(g_flocks, 0, sizeof(g_flocks));
    g_flock_next_id = 1;
    g_flock_initialized = 1;
    klog_info("vfs_ext: flock system initialized");
    return 0;
}

int vfs_ext_flock_acquire(uint32_t inode, uint32_t owner_uid, uint32_t type, uint32_t start, uint32_t len)
{
    uint32_t i;
    if (!g_flock_initialized) vfs_ext_flock_init();

    /* 检查冲突锁 */
    for (i = 0; i < VFS_EXT_MAX_LOCKS; i++) {
        if (!g_flocks[i].active) continue;
        if (g_flocks[i].inode != inode) continue;
        /* 相同所有者可以升级锁 */
        if (g_flocks[i].owner_uid == owner_uid) {
            if (g_flocks[i].lock_type == type) return 0;
            if (type == VFS_EXT_LOCK_EXCLUSIVE) {
                g_flocks[i].lock_type = VFS_EXT_LOCK_EXCLUSIVE;
                return 0;
            }
            return -1;
        }
        /* 不同所有者：检查冲突 */
        if (g_flocks[i].lock_type == VFS_EXT_LOCK_EXCLUSIVE) {
            return -1;
        }
        if (type == VFS_EXT_LOCK_EXCLUSIVE) {
            return -1;
        }
    }

    /* 分配新锁 */
    for (i = 0; i < VFS_EXT_MAX_LOCKS; i++) {
        if (!g_flocks[i].active) {
            g_flocks[i].lock_id = g_flock_next_id++;
            g_flocks[i].inode = inode;
            g_flocks[i].owner_uid = owner_uid;
            g_flocks[i].lock_type = type;
            g_flocks[i].lock_start = start;
            g_flocks[i].lock_len = len;
            g_flocks[i].created = 0;
            g_flocks[i].active = 1;
            return 0;
        }
    }
    klog_err("vfs_ext: flock table full");
    return -1;
}

int vfs_ext_flock_release(uint32_t inode, uint32_t owner_uid)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_MAX_LOCKS; i++) {
        if (g_flocks[i].active && g_flocks[i].inode == inode && g_flocks[i].owner_uid == owner_uid) {
            g_flocks[i].active = 0;
            return 0;
        }
    }
    return -1;
}

int vfs_ext_flock_test(uint32_t inode, uint32_t owner_uid, uint32_t *conflict_uid)
{
    uint32_t i;
    if (!conflict_uid) return -1;
    for (i = 0; i < VFS_EXT_MAX_LOCKS; i++) {
        if (!g_flocks[i].active) continue;
        if (g_flocks[i].inode != inode) continue;
        if (g_flocks[i].owner_uid == owner_uid) continue;
        *conflict_uid = g_flocks[i].owner_uid;
        return -1;
    }
    *conflict_uid = 0;
    return 0;
}

int vfs_ext_flock_release_all(uint32_t owner_uid)
{
    uint32_t i;
    int count = 0;
    for (i = 0; i < VFS_EXT_MAX_LOCKS; i++) {
        if (g_flocks[i].active && g_flocks[i].owner_uid == owner_uid) {
            g_flocks[i].active = 0;
            count++;
        }
    }
    return count;
}

/* ================================================================ */
/*  3) Disk Cache Management (LRU, read-ahead, write-back)           */
/* ================================================================ */

static vfs_ext_block_cache_t g_cache;
static uint32_t g_cache_tick = 0;
static int g_cache_initialized = 0;

int vfs_ext_cache_init(void)
{
    memset(&g_cache, 0, sizeof(g_cache));
    g_cache.block_count = 0;
    g_cache_tick = 0;
    g_cache_initialized = 1;
    klog_info("vfs_ext: block cache initialized");
    return 0;
}

static int cache_find_block(uint32_t inode, uint32_t offset)
{
    uint32_t i;
    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid && g_cache.blocks[i].inode == inode && g_cache.blocks[i].offset == offset) {
            g_cache.blocks[i].access_time = g_cache_tick++;
            return (int)i;
        }
    }
    return -1;
}

static int cache_evict_lru(void)
{
    uint32_t i, oldest_idx = 0;
    uint32_t oldest_time = 0xFFFFFFFF;
    int found = 0;

    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid && !g_cache.blocks[i].dirty) {
            if (g_cache.blocks[i].access_time < oldest_time) {
                oldest_time = g_cache.blocks[i].access_time;
                oldest_idx = i;
                found = 1;
            }
        }
    }

    if (!found) {
        for (i = 0; i < g_cache.block_count; i++) {
            if (g_cache.blocks[i].valid) {
                if (g_cache.blocks[i].access_time < oldest_time) {
                    oldest_time = g_cache.blocks[i].access_time;
                    oldest_idx = i;
                    found = 1;
                }
            }
        }
    }

    if (found) {
        if (g_cache.blocks[oldest_idx].dirty) {
            g_cache.blocks[oldest_idx].dirty = 0;
        }
        g_cache.blocks[oldest_idx].valid = 0;
        return (int)oldest_idx;
    }

    return -1;
}

static int cache_alloc_block(void)
{
    int slot;
    if (g_cache.block_count < VFS_EXT_CACHE_MAX_BLOCKS) {
        slot = (int)g_cache.block_count;
        g_cache.block_count++;
        return slot;
    }
    return cache_evict_lru();
}

int vfs_ext_cache_read(uint32_t inode, uint32_t offset, void *buf, uint32_t size)
{
    uint32_t block_offset, block_start, bytes_to_copy;
    uint32_t copied = 0;
    int slot;

    if (!g_cache_initialized) vfs_ext_cache_init();
    if (!buf || size == 0) return -1;

    while (copied < size) {
        block_offset = (offset + copied) % VFS_EXT_CACHE_BLOCK_SIZE;
        block_start = (offset + copied) - block_offset;

        slot = cache_find_block(inode, block_start);
        if (slot < 0) {
            slot = cache_alloc_block();
            if (slot < 0) return -1;

            memset(&g_cache.blocks[slot], 0, sizeof(g_cache.blocks[slot]));
            g_cache.blocks[slot].block_id = slot;
            g_cache.blocks[slot].inode = inode;
            g_cache.blocks[slot].offset = block_start;
            g_cache.blocks[slot].size = VFS_EXT_CACHE_BLOCK_SIZE;
            g_cache.blocks[slot].dirty = 0;
            g_cache.blocks[slot].access_time = g_cache_tick++;
            g_cache.blocks[slot].valid = 1;

            memset(g_cache.blocks[slot].data, 0, VFS_EXT_CACHE_BLOCK_SIZE);
        }

        bytes_to_copy = VFS_EXT_CACHE_BLOCK_SIZE - block_offset;
        if (copied + bytes_to_copy > size) {
            bytes_to_copy = size - copied;
        }
        memcpy((uint8_t *)buf + copied, g_cache.blocks[slot].data + block_offset, bytes_to_copy);
        copied += bytes_to_copy;
    }

    return (int)copied;
}

int vfs_ext_cache_write(uint32_t inode, uint32_t offset, const void *buf, uint32_t size)
{
    uint32_t block_offset, block_start, bytes_to_copy;
    uint32_t copied = 0;
    int slot;

    if (!g_cache_initialized) vfs_ext_cache_init();
    if (!buf || size == 0) return -1;

    while (copied < size) {
        block_offset = (offset + copied) % VFS_EXT_CACHE_BLOCK_SIZE;
        block_start = (offset + copied) - block_offset;

        slot = cache_find_block(inode, block_start);
        if (slot < 0) {
            slot = cache_alloc_block();
            if (slot < 0) return -1;

            memset(&g_cache.blocks[slot], 0, sizeof(g_cache.blocks[slot]));
            g_cache.blocks[slot].block_id = slot;
            g_cache.blocks[slot].inode = inode;
            g_cache.blocks[slot].offset = block_start;
            g_cache.blocks[slot].size = VFS_EXT_CACHE_BLOCK_SIZE;
            g_cache.blocks[slot].dirty = 0;
            g_cache.blocks[slot].access_time = g_cache_tick++;
            g_cache.blocks[slot].valid = 1;
        }

        bytes_to_copy = VFS_EXT_CACHE_BLOCK_SIZE - block_offset;
        if (copied + bytes_to_copy > size) {
            bytes_to_copy = size - copied;
        }
        memcpy(g_cache.blocks[slot].data + block_offset, (const uint8_t *)buf + copied, bytes_to_copy);
        g_cache.blocks[slot].dirty = 1;
        g_cache.blocks[slot].access_time = g_cache_tick++;
        copied += bytes_to_copy;
    }

    return (int)copied;
}

int vfs_ext_cache_flush(void)
{
    uint32_t i;
    int flushed = 0;
    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid && g_cache.blocks[i].dirty) {
            g_cache.blocks[i].dirty = 0;
            flushed++;
        }
    }
    klog_info("vfs_ext: cache flushed %d blocks", flushed);
    return flushed;
}

int vfs_ext_cache_flush_inode(uint32_t inode)
{
    uint32_t i;
    int flushed = 0;
    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid && g_cache.blocks[i].inode == inode && g_cache.blocks[i].dirty) {
            g_cache.blocks[i].dirty = 0;
            flushed++;
        }
    }
    return flushed;
}

int vfs_ext_cache_invalidate(uint32_t inode)
{
    uint32_t i;
    int invalidated = 0;
    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid && g_cache.blocks[i].inode == inode) {
            if (g_cache.blocks[i].dirty) {
                g_cache.blocks[i].dirty = 0;
            }
            g_cache.blocks[i].valid = 0;
            invalidated++;
        }
    }
    return invalidated;
}

int vfs_ext_cache_read_ahead(uint32_t inode, uint32_t offset, uint32_t count)
{
    uint32_t i;
    int loaded = 0;
    uint32_t block_start = (offset / VFS_EXT_CACHE_BLOCK_SIZE) * VFS_EXT_CACHE_BLOCK_SIZE;

    for (i = 0; i < count; i++) {
        uint32_t next_offset = block_start + (i + 1) * VFS_EXT_CACHE_BLOCK_SIZE;
        if (cache_find_block(inode, next_offset) < 0) {
            int slot = cache_alloc_block();
            if (slot >= 0) {
                memset(&g_cache.blocks[slot], 0, sizeof(g_cache.blocks[slot]));
                g_cache.blocks[slot].block_id = slot;
                g_cache.blocks[slot].inode = inode;
                g_cache.blocks[slot].offset = next_offset;
                g_cache.blocks[slot].size = VFS_EXT_CACHE_BLOCK_SIZE;
                g_cache.blocks[slot].dirty = 0;
                g_cache.blocks[slot].access_time = g_cache_tick++;
                g_cache.blocks[slot].valid = 1;
                loaded++;
            }
        }
    }
    return loaded;
}

int vfs_ext_cache_write_back(uint32_t inode)
{
    return vfs_ext_cache_flush_inode(inode);
}

void vfs_ext_cache_print_stats(void)
{
    uint32_t i, valid = 0, dirty = 0;
    for (i = 0; i < g_cache.block_count; i++) {
        if (g_cache.blocks[i].valid) {
            valid++;
            if (g_cache.blocks[i].dirty) dirty++;
        }
    }
    klog_info("vfs_ext: cache stats - total=%u, valid=%u, dirty=%u", g_cache.block_count, valid, dirty);
}

/* ================================================================ */
/*  4) Inode Management (bitmap tracking)                            */
/* ================================================================ */

static vfs_ext_inode_table_t g_inode_table;
static int g_inode_table_initialized = 0;

int vfs_ext_inode_table_init(void)
{
    memset(&g_inode_table, 0, sizeof(g_inode_table));
    g_inode_table.total_inodes = VFS_EXT_MAX_INODES;
    g_inode_table.free_inodes = VFS_EXT_MAX_INODES;
    g_inode_table.next_free_hint = 0;

    /* 预分配 inode 0（根）和 inode 1 */
    {
        uint32_t byte_idx, bit_idx;
        byte_idx = 0 / 8;
        bit_idx = 0 % 8;
        g_inode_table.bitmap[byte_idx] |= (1 << bit_idx);
        byte_idx = 1 / 8;
        bit_idx = 1 % 8;
        g_inode_table.bitmap[byte_idx] |= (1 << bit_idx);
        g_inode_table.free_inodes -= 2;
    }

    g_inode_table_initialized = 1;
    klog_info("vfs_ext: inode table initialized (%u total, %u free)", g_inode_table.total_inodes, g_inode_table.free_inodes);
    return 0;
}

uint32_t vfs_ext_inode_alloc(void)
{
    uint32_t i;
    if (!g_inode_table_initialized) vfs_ext_inode_table_init();

    if (g_inode_table.free_inodes == 0) {
        klog_err("vfs_ext: no free inodes");
        return 0;
    }

    for (i = g_inode_table.next_free_hint; i < g_inode_table.total_inodes; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        if (!(g_inode_table.bitmap[byte_idx] & (1 << bit_idx))) {
            g_inode_table.bitmap[byte_idx] |= (1 << bit_idx);
            g_inode_table.free_inodes--;
            g_inode_table.next_free_hint = i + 1;
            if (g_inode_table.next_free_hint >= g_inode_table.total_inodes) {
                g_inode_table.next_free_hint = 0;
            }
            return i;
        }
    }

    for (i = 0; i < g_inode_table.next_free_hint; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        if (!(g_inode_table.bitmap[byte_idx] & (1 << bit_idx))) {
            g_inode_table.bitmap[byte_idx] |= (1 << bit_idx);
            g_inode_table.free_inodes--;
            g_inode_table.next_free_hint = i + 1;
            return i;
        }
    }

    return 0;
}

int vfs_ext_inode_free(uint32_t inode)
{
    uint32_t byte_idx, bit_idx;
    if (!g_inode_table_initialized) vfs_ext_inode_table_init();

    if (inode >= g_inode_table.total_inodes) {
        klog_err("vfs_ext: inode %u out of range", inode);
        return -1;
    }

    byte_idx = inode / 8;
    bit_idx = inode % 8;

    if (!(g_inode_table.bitmap[byte_idx] & (1 << bit_idx))) {
        klog_err("vfs_ext: inode %u already free", inode);
        return -1;
    }

    g_inode_table.bitmap[byte_idx] &= ~(1 << bit_idx);
    g_inode_table.free_inodes++;
    if (inode < g_inode_table.next_free_hint) {
        g_inode_table.next_free_hint = inode;
    }
    return 0;
}

int vfs_ext_inode_is_allocated(uint32_t inode)
{
    uint32_t byte_idx, bit_idx;
    if (!g_inode_table_initialized) return 0;
    if (inode >= g_inode_table.total_inodes) return 0;
    byte_idx = inode / 8;
    bit_idx = inode % 8;
    return (g_inode_table.bitmap[byte_idx] & (1 << bit_idx)) ? 1 : 0;
}

uint32_t vfs_ext_inode_get_free_count(void)
{
    if (!g_inode_table_initialized) vfs_ext_inode_table_init();
    return g_inode_table.free_inodes;
}

void vfs_ext_inode_dump_usage(void)
{
    uint32_t used = g_inode_table.total_inodes - g_inode_table.free_inodes;
    klog_info("vfs_ext: inode usage - total=%u, used=%u, free=%u (%.1f%%)",
        g_inode_table.total_inodes, used, g_inode_table.free_inodes,
        (float)used / (float)g_inode_table.total_inodes * 100.0f);
}

/* ================================================================ */
/*  5) Extended Attributes (xattr)                                   */
/* ================================================================ */

static vfs_ext_xattr_entry_t g_xattr_table[VFS_EXT_XATTR_MAX_ENTRIES];
static int g_xattr_initialized = 0;

static void xattr_init(void)
{
    if (g_xattr_initialized) return;
    memset(g_xattr_table, 0, sizeof(g_xattr_table));
    g_xattr_initialized = 1;
}

static vfs_ext_xattr_entry_t *xattr_find(uint32_t inode, const char *name)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_XATTR_MAX_ENTRIES; i++) {
        if (g_xattr_table[i].active && g_xattr_table[i].inode == inode && strcmp(g_xattr_table[i].name, name) == 0) {
            return &g_xattr_table[i];
        }
    }
    return NULL;
}

static vfs_ext_xattr_entry_t *xattr_alloc(void)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_XATTR_MAX_ENTRIES; i++) {
        if (!g_xattr_table[i].active) {
            memset(&g_xattr_table[i], 0, sizeof(g_xattr_table[i]));
            g_xattr_table[i].active = 1;
            return &g_xattr_table[i];
        }
    }
    return NULL;
}

int vfs_ext_xattr_set(uint32_t inode, const char *name, const void *value, uint32_t size)
{
    vfs_ext_xattr_entry_t *entry;
    xattr_init();
    if (!name || !value || size > VFS_EXT_XATTR_MAX_VALUE) return -1;

    entry = xattr_find(inode, name);
    if (!entry) {
        entry = xattr_alloc();
        if (!entry) return -1;
        entry->inode = inode;
        strncpy(entry->name, name, sizeof(entry->name) - 1);
    }

    memcpy(entry->value, value, size);
    entry->value_len = size;
    return 0;
}

int vfs_ext_xattr_get(uint32_t inode, const char *name, void *buf, uint32_t *size)
{
    vfs_ext_xattr_entry_t *entry;
    xattr_init();
    if (!name || !buf || !size) return -1;

    entry = xattr_find(inode, name);
    if (!entry) return -1;

    if (*size < entry->value_len) {
        *size = entry->value_len;
        return -1;
    }

    memcpy(buf, entry->value, entry->value_len);
    *size = entry->value_len;
    return 0;
}

int vfs_ext_xattr_remove(uint32_t inode, const char *name)
{
    vfs_ext_xattr_entry_t *entry;
    xattr_init();
    if (!name) return -1;

    entry = xattr_find(inode, name);
    if (!entry) return -1;

    entry->active = 0;
    return 0;
}

int vfs_ext_xattr_list(uint32_t inode, char *buf, uint32_t bufsize)
{
    uint32_t i, offset = 0;
    xattr_init();
    if (!buf) return -1;

    for (i = 0; i < VFS_EXT_XATTR_MAX_ENTRIES; i++) {
        if (g_xattr_table[i].active && g_xattr_table[i].inode == inode) {
            uint32_t name_len = strlen(g_xattr_table[i].name) + 1;
            if (offset + name_len > bufsize) break;
            memcpy(buf + offset, g_xattr_table[i].name, name_len);
            offset += name_len;
        }
    }
    return (int)offset;
}

int vfs_ext_xattr_copy(uint32_t src_inode, uint32_t dst_inode)
{
    uint32_t i;
    int copied = 0;
    xattr_init();

    for (i = 0; i < VFS_EXT_XATTR_MAX_ENTRIES; i++) {
        if (g_xattr_table[i].active && g_xattr_table[i].inode == src_inode) {
            vfs_ext_xattr_set(dst_inode, g_xattr_table[i].name, g_xattr_table[i].value, g_xattr_table[i].value_len);
            copied++;
        }
    }
    return copied;
}

/* ================================================================ */
/*  6) File System Check (fsck)                                      */
/* ================================================================ */

static int fsck_check_node(vfs_ext_node_t *node, vfs_ext_fsck_result_t *result, int depth, uint32_t *visited_inodes, uint32_t *visited_count, uint32_t max_visited)
{
    vfs_ext_node_t *child;
    uint32_t i;

    if (!node || !result) return 0;
    if (depth > 1000) {
        if (result->errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
            result->errors[result->errors_found].error_type = VFS_EXT_FSCK_ERROR_CYCLE;
            result->errors[result->errors_found].inode = node->inode;
            strncpy(result->errors[result->errors_found].path, node->path, sizeof(result->errors[0].path) - 1);
            strncpy(result->errors[result->errors_found].detail, "cycle detected", sizeof(result->errors[0].detail) - 1);
            result->errors_found++;
        }
        return -1;
    }

    for (i = 0; i < *visited_count; i++) {
        if (visited_inodes[i] == node->inode) {
            if (result->errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
                result->errors[result->errors_found].error_type = VFS_EXT_FSCK_ERROR_CROSSLINK;
                result->errors[result->errors_found].inode = node->inode;
                strncpy(result->errors[result->errors_found].path, node->path, sizeof(result->errors[0].path) - 1);
                strncpy(result->errors[result->errors_found].detail, "cross-link detected", sizeof(result->errors[0].detail) - 1);
                result->errors_found++;
            }
            return -1;
        }
    }

    if (*visited_count < max_visited) {
        visited_inodes[*visited_count] = node->inode;
        (*visited_count)++;
    }

    if (node->type > VFS_EXT_TYPE_SOCKET) {
        if (result->errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
            result->errors[result->errors_found].error_type = VFS_EXT_FSCK_ERROR_BAD_TYPE;
            result->errors[result->errors_found].inode = node->inode;
            strncpy(result->errors[result->errors_found].path, node->path, sizeof(result->errors[0].path) - 1);
            strncpy(result->errors[result->errors_found].detail, "bad node type", sizeof(result->errors[0].detail) - 1);
            result->errors_found++;
        }
    }

    child = node->children;
    while (child) {
        fsck_check_node(child, result, depth + 1, visited_inodes, visited_count, max_visited);
        child = child->next;
    }

    return 0;
}

vfs_ext_fsck_result_t vfs_ext_fsck(void)
{
    vfs_ext_fsck_result_t result;
    uint32_t visited_inodes[4096];
    uint32_t visited_count = 0;

    memset(&result, 0, sizeof(result));
    result.passed = 1;

    klog_info("vfs_ext: fsck started");

    if (!g_root) {
        result.passed = 0;
        if (result.errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
            result.errors[result.errors_found].error_type = VFS_EXT_FSCK_ERROR_ORPHAN;
            result.errors[result.errors_found].inode = 0;
            strncpy(result.errors[result.errors_found].path, "/", sizeof(result.errors[0].path) - 1);
            strncpy(result.errors[result.errors_found].detail, "root not found", sizeof(result.errors[0].detail) - 1);
            result.errors_found++;
        }
        return result;
    }

    if (g_root->type != VFS_EXT_TYPE_DIR) {
        result.passed = 0;
        if (result.errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
            result.errors[result.errors_found].error_type = VFS_EXT_FSCK_ERROR_BAD_TYPE;
            result.errors[result.errors_found].inode = g_root->inode;
            strncpy(result.errors[result.errors_found].path, "/", sizeof(result.errors[0].path) - 1);
            strncpy(result.errors[result.errors_found].detail, "root is not a directory", sizeof(result.errors[0].detail) - 1);
            result.errors_found++;
        }
    }

    fsck_check_node(g_root, &result, 0, visited_inodes, &visited_count, 4096);

    if (g_inode_table_initialized) {
        uint32_t i;
        for (i = 0; i < g_inode_table.total_inodes; i++) {
            if (vfs_ext_inode_is_allocated(i)) {
                uint32_t found = 0;
                uint32_t j;
                for (j = 0; j < visited_count; j++) {
                    if (visited_inodes[j] == i) {
                        found = 1;
                        break;
                    }
                }
                if (!found && i != 0 && i != 1) {
                    result.warnings++;
                    if (result.errors_found < VFS_EXT_FSCK_MAX_ERRORS) {
                        result.errors[result.errors_found].error_type = VFS_EXT_FSCK_ERROR_ORPHAN;
                        result.errors[result.errors_found].inode = i;
                        strncpy(result.errors[result.errors_found].path, "(orphan)", sizeof(result.errors[0].path) - 1);
                        strncpy(result.errors[result.errors_found].detail, "orphan inode", sizeof(result.errors[0].detail) - 1);
                        result.errors_found++;
                    }
                }
            }
        }
    }

    if (result.errors_found > 0) {
        result.passed = 0;
    }

    klog_info("vfs_ext: fsck completed - passed=%d, errors=%d, warnings=%d",
        result.passed, result.errors_found, result.warnings);
    return result;
}

int vfs_ext_fsck_fix_orphans(void)
{
    uint32_t i;
    int fixed = 0;
    if (!g_inode_table_initialized) return 0;

    for (i = 2; i < g_inode_table.total_inodes; i++) {
        if (vfs_ext_inode_is_allocated(i)) {
            vfs_ext_inode_free(i);
            fixed++;
        }
    }
    klog_info("vfs_ext: fsck fixed %d orphan inodes", fixed);
    return fixed;
}

int vfs_ext_fsck_fix_crosslinks(void)
{
    klog_info("vfs_ext: fsck crosslink fix not implemented in simplified mode");
    return 0;
}

/* ================================================================ */
/*  7) Memory-Mapped File I/O (mmap)                                 */
/* ================================================================ */

static vfs_ext_mmap_region_t g_mmap_regions[VFS_EXT_MMAP_MAX_REGIONS];
static uint32_t g_mmap_next_id = 1;
static int g_mmap_initialized = 0;

int vfs_ext_mmap_init(void)
{
    memset(g_mmap_regions, 0, sizeof(g_mmap_regions));
    g_mmap_next_id = 1;
    g_mmap_initialized = 1;
    klog_info("vfs_ext: mmap system initialized");
    return 0;
}

int vfs_ext_mmap_map(uint32_t inode, void *addr, uint32_t offset, uint32_t size, uint32_t prot, uint32_t flags, void **out_addr)
{
    uint32_t i;
    if (!g_mmap_initialized) vfs_ext_mmap_init();
    if (!out_addr || size == 0) return -1;

    for (i = 0; i < VFS_EXT_MMAP_MAX_REGIONS; i++) {
        if (!g_mmap_regions[i].active) {
            g_mmap_regions[i].region_id = g_mmap_next_id++;
            g_mmap_regions[i].inode = (flags & VFS_EXT_MMAP_MAP_ANON) ? 0 : inode;
            g_mmap_regions[i].virt_addr = addr;
            g_mmap_regions[i].offset = offset;
            g_mmap_regions[i].size = size;
            g_mmap_regions[i].prot = prot;
            g_mmap_regions[i].flags = flags;
            g_mmap_regions[i].active = 1;

            if (flags & VFS_EXT_MMAP_MAP_ANON) {
                g_mmap_regions[i].virt_addr = kmalloc(size);
            } else {
                g_mmap_regions[i].virt_addr = kmalloc(size);
                if (g_mmap_regions[i].virt_addr) {
                    vfs_ext_cache_read(inode, offset, g_mmap_regions[i].virt_addr, size);
                }
            }

            if (!g_mmap_regions[i].virt_addr) {
                g_mmap_regions[i].active = 0;
                return -1;
            }

            *out_addr = g_mmap_regions[i].virt_addr;
            return (int)g_mmap_regions[i].region_id;
        }
    }

    klog_err("vfs_ext: mmap region table full");
    return -1;
}

int vfs_ext_mmap_unmap(void *addr, uint32_t size)
{
    uint32_t i;
    if (!g_mmap_initialized) return -1;

    for (i = 0; i < VFS_EXT_MMAP_MAX_REGIONS; i++) {
        if (g_mmap_regions[i].active && g_mmap_regions[i].virt_addr == addr && g_mmap_regions[i].size == size) {
            if ((g_mmap_regions[i].flags & VFS_EXT_MMAP_MAP_SHARED) && !(g_mmap_regions[i].flags & VFS_EXT_MMAP_MAP_ANON)) {
                if (g_mmap_regions[i].prot & VFS_EXT_MMAP_PROT_WRITE) {
                    vfs_ext_cache_write(g_mmap_regions[i].inode, g_mmap_regions[i].offset, g_mmap_regions[i].virt_addr, g_mmap_regions[i].size);
                }
            }
            if (g_mmap_regions[i].virt_addr) {
                kfree(g_mmap_regions[i].virt_addr);
            }
            g_mmap_regions[i].active = 0;
            return 0;
        }
    }
    return -1;
}

int vfs_ext_mmap_sync(void *addr, uint32_t size, int async)
{
    uint32_t i;
    if (!g_mmap_initialized) return -1;

    for (i = 0; i < VFS_EXT_MMAP_MAX_REGIONS; i++) {
        if (g_mmap_regions[i].active && g_mmap_regions[i].virt_addr == addr) {
            if (!(g_mmap_regions[i].flags & VFS_EXT_MMAP_MAP_ANON)) {
                vfs_ext_cache_write(g_mmap_regions[i].inode, g_mmap_regions[i].offset, g_mmap_regions[i].virt_addr, g_mmap_regions[i].size);
                if (!async) {
                    vfs_ext_cache_flush_inode(g_mmap_regions[i].inode);
                }
            }
            return 0;
        }
    }
    return -1;
}

int vfs_ext_mmap_protect(void *addr, uint32_t size, uint32_t prot)
{
    uint32_t i;
    if (!g_mmap_initialized) return -1;

    for (i = 0; i < VFS_EXT_MMAP_MAX_REGIONS; i++) {
        if (g_mmap_regions[i].active && g_mmap_regions[i].virt_addr == addr) {
            g_mmap_regions[i].prot = prot;
            return 0;
        }
    }
    return -1;
}

/* ================================================================ */
/*  8) Async I/O (aio)                                               */
/* ================================================================ */

/* AIO requests are queued and processed asynchronously by the scheduler tick.
 * vfs_ext_aio_process_one() is called from sched_tick() in kernel/sched.c,
 * which processes one pending request per tick, allowing other tasks to run
 * between I/O operations. */

/* Round-robin pointer for fair processing */
static uint32_t g_aio_rr_index = 0;

int vfs_ext_aio_init(void)
{
    memset(g_aio_requests, 0, sizeof(g_aio_requests));
    g_aio_next_id = 1;
    g_aio_initialized = 1;
    g_aio_rr_index = 0;
    klog_info("vfs_ext: aio system initialized (async mode)");
    return 0;
}

static int aio_alloc_request(void)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (!g_aio_requests[i].active) {
            memset(&g_aio_requests[i], 0, sizeof(g_aio_requests[i]));
            g_aio_requests[i].request_id = g_aio_next_id++;
            g_aio_requests[i].state = VFS_EXT_AIO_STATE_PENDING;
            g_aio_requests[i].active = 1;
            return (int)i;
        }
    }
    return -1;
}

/* Execute a single pending AIO request. Called from scheduler tick.
 * Returns 1 if a request was processed, 0 if none were pending. */
int vfs_ext_aio_process_one(void) {
    if (!g_aio_initialized) return 0;

    /* Round-robin search for a pending request, starting from the last index.
     * This ensures fair processing across all requests and avoids starving
     * earlier requests when many are submitted. */
    for (uint32_t scan = 0; scan < VFS_EXT_AIO_MAX_REQUESTS; scan++) {
        uint32_t idx = (g_aio_rr_index + scan) % VFS_EXT_AIO_MAX_REQUESTS;
        if (!g_aio_requests[idx].active) continue;
        if (g_aio_requests[idx].state != VFS_EXT_AIO_STATE_PENDING) continue;

        vfs_ext_aio_request_t *req = &g_aio_requests[idx];

        /* Mark as RUNNING so aio_wait / aio_poll see it is in progress */
        req->state = VFS_EXT_AIO_STATE_RUNNING;

        /* Perform the actual I/O through the buffer cache */
        if (req->type == VFS_EXT_AIO_READ) {
            req->result = vfs_ext_cache_read(req->inode, req->offset,
                                              req->buffer, req->size);
        } else if (req->type == VFS_EXT_AIO_WRITE) {
            req->result = vfs_ext_cache_write(req->inode, req->offset,
                                               req->buffer, req->size);
        } else {
            req->result = -1;
        }

        req->state = (req->result >= 0) ? VFS_EXT_AIO_STATE_DONE
                                        : VFS_EXT_AIO_STATE_ERROR;

        /* Invoke the completion callback if provided */
        if (req->callback) {
            req->callback(req->result, req->user_data);
        }

        g_aio_rr_index = (idx + 1) % VFS_EXT_AIO_MAX_REQUESTS;
        return 1;
    }

    return 0;
}

int vfs_ext_aio_read(uint32_t inode, void *buf, uint32_t offset, uint32_t size, vfs_ext_aio_callback_t cb, void *user_data, uint32_t *req_id)
{
    int slot;
    if (!g_aio_initialized) vfs_ext_aio_init();
    if (!buf || !req_id) return -1;

    slot = aio_alloc_request();
    if (slot < 0) return -1;

    g_aio_requests[slot].inode = inode;
    g_aio_requests[slot].type = VFS_EXT_AIO_READ;
    g_aio_requests[slot].offset = offset;
    g_aio_requests[slot].buffer = buf;
    g_aio_requests[slot].size = size;
    g_aio_requests[slot].callback = cb;
    g_aio_requests[slot].user_data = user_data;
    g_aio_requests[slot].submitted = 0;
    g_aio_requests[slot].completed = 0;
    g_aio_requests[slot].result = 0;

    *req_id = g_aio_requests[slot].request_id;
    return 0;
}

int vfs_ext_aio_write(uint32_t inode, const void *buf, uint32_t offset, uint32_t size, vfs_ext_aio_callback_t cb, void *user_data, uint32_t *req_id)
{
    int slot;
    if (!g_aio_initialized) vfs_ext_aio_init();
    if (!buf || !req_id) return -1;

    slot = aio_alloc_request();
    if (slot < 0) return -1;

    g_aio_requests[slot].inode = inode;
    g_aio_requests[slot].type = VFS_EXT_AIO_WRITE;
    g_aio_requests[slot].offset = offset;
    g_aio_requests[slot].buffer = (void *)buf;
    g_aio_requests[slot].size = size;
    g_aio_requests[slot].callback = cb;
    g_aio_requests[slot].user_data = user_data;
    g_aio_requests[slot].submitted = 0;
    g_aio_requests[slot].completed = 0;
    g_aio_requests[slot].result = 0;

    *req_id = g_aio_requests[slot].request_id;
    return 0;
}

int vfs_ext_aio_wait(uint32_t req_id)
{
    if (!g_aio_initialized) return -1;

    for (uint32_t i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (!g_aio_requests[i].active) continue;
        if (g_aio_requests[i].request_id != req_id) continue;

        vfs_ext_aio_request_t *req = &g_aio_requests[i];

        /* If the request is still PENDING (not yet processed by sched tick),
         * process it inline now.  This makes aio_wait() a proper synchronous
         * fallback for callers that need the result immediately. */
        if (req->state == VFS_EXT_AIO_STATE_PENDING) {
            req->state = VFS_EXT_AIO_STATE_RUNNING;
            if (req->type == VFS_EXT_AIO_READ) {
                req->result = vfs_ext_cache_read(req->inode, req->offset,
                                                  req->buffer, req->size);
            } else if (req->type == VFS_EXT_AIO_WRITE) {
                req->result = vfs_ext_cache_write(req->inode, req->offset,
                                                   req->buffer, req->size);
            } else {
                req->result = -1;
            }
            req->state = (req->result >= 0) ? VFS_EXT_AIO_STATE_DONE
                                            : VFS_EXT_AIO_STATE_ERROR;
        }

        if (req->callback) {
            req->callback(req->result, req->user_data);
        }

        return req->result;
    }
    return -1;
}

int vfs_ext_aio_poll(uint32_t req_id, int *done, int *result)
{
    if (!g_aio_initialized || !done || !result) return -1;

    for (uint32_t i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (!g_aio_requests[i].active) continue;
        if (g_aio_requests[i].request_id != req_id) continue;

        vfs_ext_aio_request_t *req = &g_aio_requests[i];
        if (req->state == VFS_EXT_AIO_STATE_DONE ||
            req->state == VFS_EXT_AIO_STATE_ERROR) {
            *done = 1;
            *result = req->result;
        } else {
            *done = 0;
            *result = 0;
        }
        return 0;
    }
    return -1;
}

int vfs_ext_aio_cancel(uint32_t req_id)
{
    if (!g_aio_initialized) return -1;

    for (uint32_t i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (!g_aio_requests[i].active) continue;
        if (g_aio_requests[i].request_id != req_id) continue;

        if (g_aio_requests[i].state == VFS_EXT_AIO_STATE_PENDING ||
            g_aio_requests[i].state == VFS_EXT_AIO_STATE_RUNNING) {
            g_aio_requests[i].active = 0;
            return 0;
        }
        return -1;
    }
    return -1;
}

/* Process all pending AIO requests.  This can be called from a periodic
 * timer interrupt or from the idle task.  It processes one request per
 * call to keep I/O non-blocking. */
int vfs_ext_aio_process_all(void)
{
    /* Process one pending request per call (round-robin) */
    return vfs_ext_aio_process_one();
}

int vfs_ext_aio_cleanup(void)
{
    uint32_t i;
    int cleaned = 0;
    if (!g_aio_initialized) return 0;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active &&
            (g_aio_requests[i].state == VFS_EXT_AIO_STATE_DONE ||
             g_aio_requests[i].state == VFS_EXT_AIO_STATE_ERROR)) {
            g_aio_requests[i].active = 0;
            cleaned++;
        }
    }
    return cleaned;
}

/* ================================================================ */
/*  9) File Snapshots (文件快照)                                     */
/* ================================================================ */

static vfs_ext_snapshot_t g_snapshots[VFS_EXT_SNAPSHOT_MAX];
static uint32_t g_snapshot_next_id = 1;
static int g_snapshot_initialized = 0;

static void snapshot_init(void)
{
    if (g_snapshot_initialized) return;
    memset(g_snapshots, 0, sizeof(g_snapshots));
    g_snapshot_next_id = 1;
    g_snapshot_initialized = 1;
}

static int snapshot_find_by_id(uint32_t snap_id)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_SNAPSHOT_MAX; i++) {
        if (g_snapshots[i].active && g_snapshots[i].snap_id == snap_id) {
            return (int)i;
        }
    }
    return -1;
}

static int snapshot_alloc_slot(void)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_SNAPSHOT_MAX; i++) {
        if (!g_snapshots[i].active) {
            memset(&g_snapshots[i], 0, sizeof(g_snapshots[i]));
            g_snapshots[i].active = 1;
            return (int)i;
        }
    }
    return -1;
}

int vfs_ext_snapshot_create(uint32_t inode, const char *name, uint32_t *snap_id)
{
    vfs_ext_node_t *node;
    int slot;
    uint32_t copy_size;

    snapshot_init();
    if (!snap_id) return -22;

    node = g_root;
    {
        vfs_ext_node_t *cur = g_root;
        vfs_ext_node_t *found = NULL;
        vfs_ext_node_t *stack[1024];
        int depth = 0;
        stack[depth++] = cur;
        while (depth > 0) {
            cur = stack[--depth];
            if (cur->inode == inode) {
                found = cur;
                break;
            }
            cur = cur->children;
            while (cur) {
                if (depth < 1024) {
                    stack[depth++] = cur;
                }
                cur = cur->next;
            }
        }
        node = found;
    }

    if (!node) {
        klog_err("vfs_ext: snapshot create - inode %u not found", inode);
        return -2;
    }
    if (node->type != VFS_EXT_TYPE_FILE) {
        klog_err("vfs_ext: snapshot create - inode %u is not a file", inode);
        return -22;
    }
    if (node->size > VFS_EXT_SNAPSHOT_DATA_MAX) {
        klog_err("vfs_ext: snapshot create - file too large (%u bytes)", node->size);
        return -12;
    }

    slot = snapshot_alloc_slot();
    if (slot < 0) {
        klog_err("vfs_ext: snapshot table full");
        return -12;
    }

    g_snapshots[slot].snap_id = g_snapshot_next_id++;
    g_snapshots[slot].inode = inode;
    g_snapshots[slot].size = node->size;
    g_snapshots[slot].created = 0;
    g_snapshots[slot].active = 1;

    if (name) {
        strncpy(g_snapshots[slot].name, name, VFS_EXT_SNAPSHOT_NAME_MAX - 1);
    }
    strncpy(g_snapshots[slot].path, node->path, sizeof(g_snapshots[slot].path) - 1);

    copy_size = node->size;
    if (copy_size > VFS_EXT_SNAPSHOT_DATA_MAX) {
        copy_size = VFS_EXT_SNAPSHOT_DATA_MAX;
    }
    if (node->fs_data && copy_size > 0) {
        memcpy(g_snapshots[slot].data, node->fs_data, copy_size);
    }

    *snap_id = g_snapshots[slot].snap_id;
    klog_info("vfs_ext: snapshot created id=%u inode=%u size=%u",
        g_snapshots[slot].snap_id, inode, copy_size);
    return 0;
}

int vfs_ext_snapshot_restore(uint32_t snap_id)
{
    vfs_ext_node_t *node;
    int slot;
    void *new_data;

    snapshot_init();

    slot = snapshot_find_by_id(snap_id);
    if (slot < 0) {
        klog_err("vfs_ext: snapshot restore - snapshot %u not found", snap_id);
        return -2;
    }

    {
        vfs_ext_node_t *cur = g_root;
        vfs_ext_node_t *found = NULL;
        vfs_ext_node_t *stack[1024];
        int depth = 0;
        stack[depth++] = cur;
        while (depth > 0) {
            cur = stack[--depth];
            if (cur->inode == g_snapshots[slot].inode) {
                found = cur;
                break;
            }
            cur = cur->children;
            while (cur) {
                if (depth < 1024) {
                    stack[depth++] = cur;
                }
                cur = cur->next;
            }
        }
        node = found;
    }

    if (!node) {
        klog_err("vfs_ext: snapshot restore - inode %u not found", g_snapshots[slot].inode);
        return -2;
    }

    new_data = kmalloc(g_snapshots[slot].size);
    if (!new_data && g_snapshots[slot].size > 0) {
        klog_err("vfs_ext: snapshot restore - memory allocation failed");
        return -12;
    }

    if (node->fs_data) {
        kfree(node->fs_data);
    }

    if (g_snapshots[slot].size > 0) {
        memcpy(new_data, g_snapshots[slot].data, g_snapshots[slot].size);
    }
    node->fs_data = new_data;
    node->size = g_snapshots[slot].size;

    klog_info("vfs_ext: snapshot restored id=%u inode=%u size=%u",
        snap_id, g_snapshots[slot].inode, g_snapshots[slot].size);
    return 0;
}

int vfs_ext_snapshot_delete(uint32_t snap_id)
{
    int slot;

    snapshot_init();

    slot = snapshot_find_by_id(snap_id);
    if (slot < 0) {
        klog_err("vfs_ext: snapshot delete - snapshot %u not found", snap_id);
        return -2;
    }

    g_snapshots[slot].active = 0;
    klog_info("vfs_ext: snapshot deleted id=%u", snap_id);
    return 0;
}

int vfs_ext_snapshot_list(uint32_t inode, vfs_ext_snapshot_t *buf, uint32_t max_count)
{
    uint32_t i;
    int count = 0;

    snapshot_init();
    if (!buf || max_count == 0) return -22;

    for (i = 0; i < VFS_EXT_SNAPSHOT_MAX; i++) {
        if (g_snapshots[i].active && g_snapshots[i].inode == inode) {
            if ((uint32_t)count >= max_count) break;
            memcpy(&buf[count], &g_snapshots[i], sizeof(vfs_ext_snapshot_t));
            count++;
        }
    }
    return count;
}

int vfs_ext_snapshot_get(uint32_t snap_id, vfs_ext_snapshot_t *snap)
{
    int slot;

    snapshot_init();
    if (!snap) return -22;

    slot = snapshot_find_by_id(snap_id);
    if (slot < 0) {
        klog_err("vfs_ext: snapshot get - snapshot %u not found", snap_id);
        return -2;
    }

    memcpy(snap, &g_snapshots[slot], sizeof(vfs_ext_snapshot_t));
    return 0;
}

/* ================================================================ */
/*  10) File Versioning (文件版本控制)                               */
/* ================================================================ */

static vfs_ext_version_t g_versions[VFS_EXT_VERSION_MAX];
static uint32_t g_version_next = 1;
static int g_version_initialized = 0;

static void version_init(void)
{
    if (g_version_initialized) return;
    memset(g_versions, 0, sizeof(g_versions));
    g_version_next = 1;
    g_version_initialized = 1;
}

static int version_find(uint32_t inode, uint32_t version)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_VERSION_MAX; i++) {
        if (g_versions[i].active && g_versions[i].inode == inode && g_versions[i].version == version) {
            return (int)i;
        }
    }
    return -1;
}

static int version_alloc_slot(void)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_VERSION_MAX; i++) {
        if (!g_versions[i].active) {
            memset(&g_versions[i], 0, sizeof(g_versions[i]));
            g_versions[i].active = 1;
            return (int)i;
        }
    }
    return -1;
}

static vfs_ext_node_t *find_node_by_inode(uint32_t inode)
{
    vfs_ext_node_t *cur = g_root;
    vfs_ext_node_t *stack[1024];
    int depth = 0;

    if (!g_root) return NULL;
    stack[depth++] = cur;
    while (depth > 0) {
        cur = stack[--depth];
        if (cur->inode == inode) {
            return cur;
        }
        cur = cur->children;
        while (cur) {
            if (depth < 1024) {
                stack[depth++] = cur;
            }
            cur = cur->next;
        }
    }
    return NULL;
}

int vfs_ext_version_save(uint32_t inode, const char *comment, uint32_t *version)
{
    vfs_ext_node_t *node;
    int slot;
    uint32_t copy_size;

    version_init();
    if (!version) return -22;

    node = find_node_by_inode(inode);
    if (!node) {
        klog_err("vfs_ext: version save - inode %u not found", inode);
        return -2;
    }
    if (node->type != VFS_EXT_TYPE_FILE) {
        klog_err("vfs_ext: version save - inode %u is not a file", inode);
        return -22;
    }
    if (node->size > VFS_EXT_VERSION_DATA_MAX) {
        klog_err("vfs_ext: version save - file too large (%u bytes)", node->size);
        return -12;
    }

    slot = version_alloc_slot();
    if (slot < 0) {
        klog_err("vfs_ext: version table full");
        return -12;
    }

    g_versions[slot].version = g_version_next++;
    g_versions[slot].inode = inode;
    g_versions[slot].size = node->size;
    g_versions[slot].modified = 0;
    g_versions[slot].active = 1;

    if (comment) {
        strncpy(g_versions[slot].comment, comment, sizeof(g_versions[slot].comment) - 1);
    }

    copy_size = node->size;
    if (copy_size > VFS_EXT_VERSION_DATA_MAX) {
        copy_size = VFS_EXT_VERSION_DATA_MAX;
    }
    if (node->fs_data && copy_size > 0) {
        memcpy(g_versions[slot].data, node->fs_data, copy_size);
    }

    *version = g_versions[slot].version;
    klog_info("vfs_ext: version saved v%u inode=%u size=%u",
        g_versions[slot].version, inode, copy_size);
    return 0;
}

int vfs_ext_version_restore(uint32_t inode, uint32_t version)
{
    vfs_ext_node_t *node;
    int slot;
    void *new_data;

    version_init();

    slot = version_find(inode, version);
    if (slot < 0) {
        klog_err("vfs_ext: version restore - version %u not found", version);
        return -2;
    }

    node = find_node_by_inode(inode);
    if (!node) {
        klog_err("vfs_ext: version restore - inode %u not found", inode);
        return -2;
    }

    new_data = kmalloc(g_versions[slot].size);
    if (!new_data && g_versions[slot].size > 0) {
        klog_err("vfs_ext: version restore - memory allocation failed");
        return -12;
    }

    if (node->fs_data) {
        kfree(node->fs_data);
    }

    if (g_versions[slot].size > 0) {
        memcpy(new_data, g_versions[slot].data, g_versions[slot].size);
    }
    node->fs_data = new_data;
    node->size = g_versions[slot].size;

    klog_info("vfs_ext: version restored v%u inode=%u size=%u",
        version, inode, g_versions[slot].size);
    return 0;
}

int vfs_ext_version_delete(uint32_t inode, uint32_t version)
{
    int slot;

    version_init();

    slot = version_find(inode, version);
    if (slot < 0) {
        klog_err("vfs_ext: version delete - version %u not found", version);
        return -2;
    }

    g_versions[slot].active = 0;
    klog_info("vfs_ext: version deleted v%u inode=%u", version, inode);
    return 0;
}

int vfs_ext_version_list(uint32_t inode, vfs_ext_version_t *buf, uint32_t max_count)
{
    uint32_t i;
    int count = 0;

    version_init();
    if (!buf || max_count == 0) return -22;

    for (i = 0; i < VFS_EXT_VERSION_MAX; i++) {
        if (g_versions[i].active && g_versions[i].inode == inode) {
            if ((uint32_t)count >= max_count) break;
            memcpy(&buf[count], &g_versions[i], sizeof(vfs_ext_version_t));
            count++;
        }
    }
    return count;
}

int vfs_ext_version_diff(uint32_t inode, uint32_t v1, uint32_t v2, char *diff_buf, uint32_t diff_size)
{
    int slot1, slot2;
    uint32_t i;
    uint32_t diff_count = 0;
    uint32_t min_size;
    uint32_t offset = 0;
    char line[128];
    int line_len;

    version_init();
    if (!diff_buf || diff_size == 0) return -22;

    slot1 = version_find(inode, v1);
    slot2 = version_find(inode, v2);
    if (slot1 < 0 || slot2 < 0) {
        klog_err("vfs_ext: version diff - version not found");
        return -2;
    }

    diff_buf[0] = '\0';

    if (g_versions[slot1].size != g_versions[slot2].size) {
        line_len = snprintf(line, sizeof(line), "size diff: %u vs %u\n",
            g_versions[slot1].size, g_versions[slot2].size);
        if (offset + (uint32_t)line_len < diff_size) {
            memcpy(diff_buf + offset, line, (uint32_t)line_len);
            offset += (uint32_t)line_len;
        }
    }

    min_size = g_versions[slot1].size;
    if (g_versions[slot2].size < min_size) {
        min_size = g_versions[slot2].size;
    }

    for (i = 0; i < min_size && diff_count < 100; i++) {
        if (g_versions[slot1].data[i] != g_versions[slot2].data[i]) {
            diff_count++;
            line_len = snprintf(line, sizeof(line), "  byte %u: 0x%02x vs 0x%02x\n",
                i, g_versions[slot1].data[i], g_versions[slot2].data[i]);
            if (offset + (uint32_t)line_len < diff_size) {
                memcpy(diff_buf + offset, line, (uint32_t)line_len);
                offset += (uint32_t)line_len;
            }
        }
    }

    if (diff_count >= 100) {
        line_len = snprintf(line, sizeof(line), "... and more differences\n");
        if (offset + (uint32_t)line_len < diff_size) {
            memcpy(diff_buf + offset, line, (uint32_t)line_len);
            offset += (uint32_t)line_len;
        }
    }

    line_len = snprintf(line, sizeof(line), "total differences: %u\n", diff_count);
    if (offset + (uint32_t)line_len < diff_size) {
        memcpy(diff_buf + offset, line, (uint32_t)line_len);
        offset += (uint32_t)line_len;
    }

    diff_buf[offset] = '\0';
    return (int)diff_count;
}

/* ================================================================ */
/*  11) Directory Watch (目录监视)                                   */
/* ================================================================ */

static vfs_ext_watch_t g_watches[VFS_EXT_WATCH_MAX];
static uint32_t g_watch_next_id = 1;
static int g_watch_initialized = 0;

int vfs_ext_watch_init(void)
{
    memset(g_watches, 0, sizeof(g_watches));
    g_watch_next_id = 1;
    g_watch_initialized = 1;
    klog_info("vfs_ext: directory watch system initialized");
    return 0;
}

static int watch_find_by_id(uint32_t watch_id)
{
    uint32_t i;
    for (i = 0; i < VFS_EXT_WATCH_MAX; i++) {
        if (g_watches[i].active && g_watches[i].watch_id == watch_id) {
            return (int)i;
        }
    }
    return -1;
}

int vfs_ext_watch_add(const char *path, uint32_t event_mask,
                       vfs_ext_watch_callback_t callback, void *user_data,
                       uint32_t *watch_id)
{
    vfs_ext_node_t *node;
    uint32_t i;

    if (!g_watch_initialized) vfs_ext_watch_init();
    if (!path || !watch_id) return -22;

    node = vfs_ext_resolve(path);
    if (!node) {
        klog_err("vfs_ext: watch add - path %s not found", path);
        return -2;
    }
    if (node->type != VFS_EXT_TYPE_DIR) {
        klog_err("vfs_ext: watch add - %s is not a directory", path);
        return -22;
    }

    for (i = 0; i < VFS_EXT_WATCH_MAX; i++) {
        if (!g_watches[i].active) {
            memset(&g_watches[i], 0, sizeof(g_watches[i]));
            g_watches[i].watch_id = g_watch_next_id++;
            strncpy(g_watches[i].path, path, sizeof(g_watches[i].path) - 1);
            g_watches[i].event_mask = event_mask;
            g_watches[i].callback = callback;
            g_watches[i].user_data = user_data;
            g_watches[i].active = 1;
            *watch_id = g_watches[i].watch_id;
            klog_info("vfs_ext: watch added id=%u path=%s mask=0x%x",
                g_watches[i].watch_id, path, event_mask);
            return 0;
        }
    }

    klog_err("vfs_ext: watch table full");
    return -12;
}

int vfs_ext_watch_remove(uint32_t watch_id)
{
    int slot;

    if (!g_watch_initialized) return -22;

    slot = watch_find_by_id(watch_id);
    if (slot < 0) {
        klog_err("vfs_ext: watch remove - watch %u not found", watch_id);
        return -2;
    }

    g_watches[slot].active = 0;
    klog_info("vfs_ext: watch removed id=%u", watch_id);
    return 0;
}

int vfs_ext_watch_poll(uint32_t watch_id, vfs_ext_watch_event_t *events,
                        uint32_t max_events, uint32_t *event_count)
{
    if (!g_watch_initialized) return -22;
    if (!events || !event_count) return -22;

    if (watch_find_by_id(watch_id) < 0) {
        klog_err("vfs_ext: watch poll - watch %u not found", watch_id);
        return -2;
    }

    *event_count = 0;
    return 0;
}

int vfs_ext_watch_notify(const char *path, uint32_t event_type,
                          const char *name, const char *old_name)
{
    uint32_t i;
    vfs_ext_watch_event_t event;
    int path_len;

    if (!g_watch_initialized || !path || !name) return -22;

    path_len = strlen(path);

    for (i = 0; i < VFS_EXT_WATCH_MAX; i++) {
        if (!g_watches[i].active) continue;
        if (!(g_watches[i].event_mask & event_type)) continue;

        if (strncmp(g_watches[i].path, path, path_len) == 0 &&
            (path[path_len] == '\0' || path[path_len] == '/')) {
            memset(&event, 0, sizeof(event));
            event.watch_id = g_watches[i].watch_id;
            event.event_type = event_type;
            strncpy(event.name, name, sizeof(event.name) - 1);
            if (old_name) {
                strncpy(event.old_name, old_name, sizeof(event.old_name) - 1);
            }
            event.timestamp = 0;

            if (g_watches[i].callback) {
                g_watches[i].callback(&event, g_watches[i].user_data);
            }
        }
    }
    return 0;
}

/* ================================================================ */
/*  12) File Search (文件搜索)                                       */
/* ================================================================ */

static int wildcard_match(const char *pattern, const char *str, int case_sensitive)
{
    const char *p = pattern;
    const char *s = str;
    const char *star = NULL;
    const char *s_save = NULL;

    while (*s) {
        if (*p == '*') {
            if (!*++p) return 1;
            star = p;
            s_save = s;
        } else {
            char c1 = *p;
            char c2 = *s;
            if (!case_sensitive) {
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            }
            if (c1 == c2 || *p == '?') {
                p++;
                s++;
            } else if (star) {
                p = star;
                s = ++s_save;
            } else {
                return 0;
            }
        }
    }
    while (*p == '*') p++;
    return (*p == '\0') ? 1 : 0;
}

static int search_match_file(vfs_ext_node_t *node, const vfs_ext_search_params_t *params)
{
    const char *name;
    const char *ext;

    name = node->path;

    if (params->search_flags & VFS_EXT_SEARCH_BY_NAME) {
        if (!wildcard_match(params->name_pattern, name, params->case_sensitive)) {
            return 0;
        }
    }

    if (params->search_flags & VFS_EXT_SEARCH_BY_EXT) {
        ext = strrchr(name, '.');
        if (ext) {
            ext++;
        }
        if (!ext || !wildcard_match(params->ext_pattern, ext, params->case_sensitive)) {
            return 0;
        }
    }

    if (params->search_flags & VFS_EXT_SEARCH_BY_SIZE) {
        if (node->size < params->min_size || node->size > params->max_size) {
            return 0;
        }
    }

    if (params->search_flags & VFS_EXT_SEARCH_BY_DATE) {
        if (node->modified < params->min_date || node->modified > params->max_date) {
            return 0;
        }
    }

    return 1;
}

static void search_build_full_path(vfs_ext_node_t *node, char *buf, int buf_size)
{
    vfs_ext_node_t *stack[256];
    int depth = 0;
    int offset = 0;
    int i;

    while (node && depth < 256) {
        stack[depth++] = node;
        node = node->parent;
    }

    buf[0] = '\0';
    for (i = depth - 1; i >= 0; i--) {
        if (i == depth - 1 && strcmp(stack[i]->path, "/") == 0) {
            strncpy(buf, "/", buf_size - 1);
            offset = 1;
        } else {
            if (offset > 0 && buf[offset - 1] != '/') {
                if (offset < buf_size - 1) {
                    buf[offset++] = '/';
                    buf[offset] = '\0';
                }
            }
            strncat(buf, stack[i]->path, buf_size - offset - 1);
            offset = strlen(buf);
        }
    }
}

static int search_recursive(vfs_ext_node_t *dir, const vfs_ext_search_params_t *params,
                             vfs_ext_search_result_t *results, uint32_t max_results,
                             uint32_t *result_count)
{
    vfs_ext_node_t *child;
    char full_path[VFS_EXT_SEARCH_NAME_MAX];

    if (!dir || !results || !result_count) return -22;

    child = dir->children;
    while (child) {
        if (child->type == VFS_EXT_TYPE_DIR) {
            if (params->recursive) {
                search_recursive(child, params, results, max_results, result_count);
            }
        } else if (child->type == VFS_EXT_TYPE_FILE) {
            if (search_match_file(child, params)) {
                if (*result_count < max_results) {
                    search_build_full_path(child, full_path, sizeof(full_path));
                    strncpy(results[*result_count].path, full_path, sizeof(results[0].path) - 1);
                    results[*result_count].inode = child->inode;
                    results[*result_count].size = child->size;
                    results[*result_count].type = child->type;
                    results[*result_count].modified = child->modified;
                    (*result_count)++;
                }
            }
        }
        child = child->next;
    }
    return 0;
}

int vfs_ext_search(const char *base_path, const vfs_ext_search_params_t *params,
                    vfs_ext_search_result_t *results, uint32_t max_results,
                    uint32_t *result_count)
{
    vfs_ext_node_t *base_dir;

    if (!base_path || !params || !results || !result_count) return -22;
    if (max_results == 0) return -22;
    if (max_results > VFS_EXT_SEARCH_MAX_RESULTS) {
        max_results = VFS_EXT_SEARCH_MAX_RESULTS;
    }

    base_dir = vfs_ext_resolve(base_path);
    if (!base_dir) {
        klog_err("vfs_ext: search - base path %s not found", base_path);
        return -2;
    }
    if (base_dir->type != VFS_EXT_TYPE_DIR) {
        klog_err("vfs_ext: search - %s is not a directory", base_path);
        return -22;
    }

    *result_count = 0;
    search_recursive(base_dir, params, results, max_results, result_count);

    klog_info("vfs_ext: search completed - %u results", *result_count);
    return 0;
}

/* ================================================================ */
/*  13) File Hashing (文件哈希)                                      */
/* ================================================================ */

static const uint32_t g_crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static uint32_t crc32_calculate(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;

    if (!data || len == 0) return 0;

    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ g_crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

static void md5_simulate(const uint8_t *data, uint32_t len, uint8_t *hash)
{
    uint32_t a, b, c, d;
    uint32_t i;

    a = 0x67452301;
    b = 0xEFCDAB89;
    c = 0x98BADCFE;
    d = 0x10325476;

    for (i = 0; i < len; i++) {
        uint32_t val = data[i];
        a = a ^ (val + 0x5A827999);
        b = b ^ ((val << 3) | (val >> 5));
        c = c + (val * 0x9E3779B9);
        d = d ^ ((val << 7) | (val >> 1));
        a = (a << 5) | (a >> 27);
        b = (b << 3) | (b >> 29);
        c = (c << 7) | (c >> 25);
        d = (d << 11) | (d >> 21);
    }

    hash[0] = (uint8_t)(a & 0xFF);
    hash[1] = (uint8_t)((a >> 8) & 0xFF);
    hash[2] = (uint8_t)((a >> 16) & 0xFF);
    hash[3] = (uint8_t)((a >> 24) & 0xFF);
    hash[4] = (uint8_t)(b & 0xFF);
    hash[5] = (uint8_t)((b >> 8) & 0xFF);
    hash[6] = (uint8_t)((b >> 16) & 0xFF);
    hash[7] = (uint8_t)((b >> 24) & 0xFF);
    hash[8] = (uint8_t)(c & 0xFF);
    hash[9] = (uint8_t)((c >> 8) & 0xFF);
    hash[10] = (uint8_t)((c >> 16) & 0xFF);
    hash[11] = (uint8_t)((c >> 24) & 0xFF);
    hash[12] = (uint8_t)(d & 0xFF);
    hash[13] = (uint8_t)((d >> 8) & 0xFF);
    hash[14] = (uint8_t)((d >> 16) & 0xFF);
    hash[15] = (uint8_t)((d >> 24) & 0xFF);
}

static void sha1_simulate(const uint8_t *data, uint32_t len, uint8_t *hash)
{
    uint32_t h0, h1, h2, h3, h4;
    uint32_t i;

    h0 = 0x67452301;
    h1 = 0xEFCDAB89;
    h2 = 0x98BADCFE;
    h3 = 0x10325476;
    h4 = 0xC3D2E1F0;

    for (i = 0; i < len; i++) {
        uint32_t val = data[i];
        h0 = h0 + (val ^ 0xA5);
        h1 = h1 ^ (val + h0);
        h2 = h2 + ((h1 << 5) | (h1 >> 27));
        h3 = h3 ^ (h2 + val);
        h4 = h4 + ((h3 << 3) | (h3 >> 29));
    }

    hash[0] = (uint8_t)((h0 >> 24) & 0xFF);
    hash[1] = (uint8_t)((h0 >> 16) & 0xFF);
    hash[2] = (uint8_t)((h0 >> 8) & 0xFF);
    hash[3] = (uint8_t)(h0 & 0xFF);
    hash[4] = (uint8_t)((h1 >> 24) & 0xFF);
    hash[5] = (uint8_t)((h1 >> 16) & 0xFF);
    hash[6] = (uint8_t)((h1 >> 8) & 0xFF);
    hash[7] = (uint8_t)(h1 & 0xFF);
    hash[8] = (uint8_t)((h2 >> 24) & 0xFF);
    hash[9] = (uint8_t)((h2 >> 16) & 0xFF);
    hash[10] = (uint8_t)((h2 >> 8) & 0xFF);
    hash[11] = (uint8_t)(h2 & 0xFF);
    hash[12] = (uint8_t)((h3 >> 24) & 0xFF);
    hash[13] = (uint8_t)((h3 >> 16) & 0xFF);
    hash[14] = (uint8_t)((h3 >> 8) & 0xFF);
    hash[15] = (uint8_t)(h3 & 0xFF);
    hash[16] = (uint8_t)((h4 >> 24) & 0xFF);
    hash[17] = (uint8_t)((h4 >> 16) & 0xFF);
    hash[18] = (uint8_t)((h4 >> 8) & 0xFF);
    hash[19] = (uint8_t)(h4 & 0xFF);
}

int vfs_ext_hash_file(uint32_t inode, uint32_t hash_type,
                       uint8_t *hash_buf, uint32_t hash_size)
{
    vfs_ext_node_t *node;
    uint32_t crc;

    if (!hash_buf) return -22;

    node = find_node_by_inode(inode);
    if (!node) {
        klog_err("vfs_ext: hash file - inode %u not found", inode);
        return -2;
    }
    if (node->type != VFS_EXT_TYPE_FILE) {
        klog_err("vfs_ext: hash file - inode %u is not a file", inode);
        return -22;
    }

    switch (hash_type) {
    case VFS_EXT_HASH_CRC32:
        if (hash_size < 4) return -22;
        crc = crc32_calculate((const uint8_t *)node->fs_data, node->size);
        hash_buf[0] = (uint8_t)((crc >> 24) & 0xFF);
        hash_buf[1] = (uint8_t)((crc >> 16) & 0xFF);
        hash_buf[2] = (uint8_t)((crc >> 8) & 0xFF);
        hash_buf[3] = (uint8_t)(crc & 0xFF);
        return 4;

    case VFS_EXT_HASH_MD5:
        if (hash_size < VFS_EXT_HASH_MD5_SIZE) return -22;
        md5_simulate((const uint8_t *)node->fs_data, node->size, hash_buf);
        return VFS_EXT_HASH_MD5_SIZE;

    case VFS_EXT_HASH_SHA1:
        if (hash_size < VFS_EXT_HASH_SHA1_SIZE) return -22;
        sha1_simulate((const uint8_t *)node->fs_data, node->size, hash_buf);
        return VFS_EXT_HASH_SHA1_SIZE;

    default:
        klog_err("vfs_ext: hash file - unsupported hash type %u", hash_type);
        return -22;
    }
}

int vfs_ext_hash_file_at(const char *path, uint32_t hash_type,
                          uint8_t *hash_buf, uint32_t hash_size)
{
    vfs_ext_node_t *node;

    if (!path || !hash_buf) return -22;

    node = vfs_ext_resolve(path);
    if (!node) {
        klog_err("vfs_ext: hash file at - %s not found", path);
        return -2;
    }

    return vfs_ext_hash_file(node->inode, hash_type, hash_buf, hash_size);
}

void vfs_ext_hash_to_hex(const uint8_t *hash, uint32_t hash_len,
                          char *hex_buf, uint32_t hex_size)
{
    static const char hex_chars[] = "0123456789abcdef";
    uint32_t i;

    if (!hash || !hex_buf || hex_size == 0) return;

    for (i = 0; i < hash_len && i * 2 + 1 < hex_size; i++) {
        hex_buf[i * 2] = hex_chars[(hash[i] >> 4) & 0x0F];
        hex_buf[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    if (i * 2 < hex_size) {
        hex_buf[i * 2] = '\0';
    } else {
        hex_buf[hex_size - 1] = '\0';
    }
}

/* ================================================================ */
/*  14) File Compression (文件压缩/解压)                             */
/* ================================================================ */

static int rle_compress(const uint8_t *src, uint32_t src_len,
                         uint8_t *dst, uint32_t dst_size,
                         uint32_t *compressed_len)
{
    uint32_t i = 0;
    uint32_t out = 0;

    if (!src || !dst || !compressed_len) return -22;
    *compressed_len = 0;

    while (i < src_len) {
        uint8_t current = src[i];
        uint32_t count = 1;

        while (i + count < src_len && src[i + count] == current && count < 255) {
            count++;
        }

        if (out + 2 > dst_size) return -12;

        dst[out++] = (uint8_t)count;
        dst[out++] = current;
        i += count;
    }

    *compressed_len = out;
    return 0;
}

static int rle_decompress(const uint8_t *src, uint32_t src_len,
                           uint8_t *dst, uint32_t dst_size,
                           uint32_t *decompressed_len)
{
    uint32_t i = 0;
    uint32_t out = 0;

    if (!src || !dst || !decompressed_len) return -22;
    *decompressed_len = 0;

    while (i + 1 < src_len) {
        uint32_t count = src[i];
        uint8_t value = src[i + 1];
        uint32_t j;

        if (out + count > dst_size) return -12;

        for (j = 0; j < count; j++) {
            dst[out++] = value;
        }
        i += 2;
    }

    *decompressed_len = out;
    return 0;
}

int vfs_ext_compress_buffer(const uint8_t *src, uint32_t src_len,
                             uint8_t *dst, uint32_t dst_size,
                             uint32_t algo, uint32_t *compressed_len)
{
    if (!src || !dst || !compressed_len) return -22;
    if (src_len == 0) {
        *compressed_len = 0;
        return 0;
    }

    switch (algo) {
    case VFS_EXT_COMPRESS_RLE:
        return rle_compress(src, src_len, dst, dst_size, compressed_len);
    case VFS_EXT_COMPRESS_NONE:
        if (src_len > dst_size) return -12;
        memcpy(dst, src, src_len);
        *compressed_len = src_len;
        return 0;
    default:
        klog_err("vfs_ext: compress buffer - unsupported algorithm %u", algo);
        return -22;
    }
}

int vfs_ext_decompress_buffer(const uint8_t *src, uint32_t src_len,
                               uint8_t *dst, uint32_t dst_size,
                               uint32_t algo, uint32_t *decompressed_len)
{
    if (!src || !dst || !decompressed_len) return -22;
    if (src_len == 0) {
        *decompressed_len = 0;
        return 0;
    }

    switch (algo) {
    case VFS_EXT_COMPRESS_RLE:
        return rle_decompress(src, src_len, dst, dst_size, decompressed_len);
    case VFS_EXT_COMPRESS_NONE:
        if (src_len > dst_size) return -12;
        memcpy(dst, src, src_len);
        *decompressed_len = src_len;
        return 0;
    default:
        klog_err("vfs_ext: decompress buffer - unsupported algorithm %u", algo);
        return -22;
    }
}

int vfs_ext_compress_file(uint32_t src_inode, uint32_t dst_inode,
                           uint32_t algo, uint32_t *compressed_size)
{
    vfs_ext_node_t *src_node;
    vfs_ext_node_t *dst_node;
    uint8_t *compressed_buf;
    uint32_t comp_len;
    int ret;

    if (!compressed_size) return -22;

    src_node = find_node_by_inode(src_inode);
    dst_node = find_node_by_inode(dst_inode);
    if (!src_node || !dst_node) {
        klog_err("vfs_ext: compress file - inode not found");
        return -2;
    }
    if (src_node->type != VFS_EXT_TYPE_FILE || dst_node->type != VFS_EXT_TYPE_FILE) {
        klog_err("vfs_ext: compress file - not a file");
        return -22;
    }

    compressed_buf = (uint8_t *)kmalloc(src_node->size * 2 + 1024);
    if (!compressed_buf) {
        klog_err("vfs_ext: compress file - memory allocation failed");
        return -12;
    }

    ret = vfs_ext_compress_buffer((const uint8_t *)src_node->fs_data, src_node->size,
                                   compressed_buf, src_node->size * 2 + 1024,
                                   algo, &comp_len);
    if (ret != 0) {
        kfree(compressed_buf);
        return ret;
    }

    if (dst_node->fs_data) {
        kfree(dst_node->fs_data);
    }
    dst_node->fs_data = kmalloc(comp_len);
    if (!dst_node->fs_data && comp_len > 0) {
        kfree(compressed_buf);
        return -12;
    }
    if (comp_len > 0) {
        memcpy(dst_node->fs_data, compressed_buf, comp_len);
    }
    dst_node->size = comp_len;

    kfree(compressed_buf);
    *compressed_size = comp_len;
    klog_info("vfs_ext: file compressed %u -> %u bytes (ratio=%.1f%%)",
        src_node->size, comp_len,
        src_node->size > 0 ? (float)comp_len / (float)src_node->size * 100.0f : 0.0f);
    return 0;
}

int vfs_ext_decompress_file(uint32_t src_inode, uint32_t dst_inode,
                             uint32_t algo, uint32_t *decompressed_size)
{
    vfs_ext_node_t *src_node;
    vfs_ext_node_t *dst_node;
    uint8_t *decomp_buf;
    uint32_t decomp_len;
    int ret;

    if (!decompressed_size) return -22;

    src_node = find_node_by_inode(src_inode);
    dst_node = find_node_by_inode(dst_inode);
    if (!src_node || !dst_node) {
        klog_err("vfs_ext: decompress file - inode not found");
        return -2;
    }
    if (src_node->type != VFS_EXT_TYPE_FILE || dst_node->type != VFS_EXT_TYPE_FILE) {
        klog_err("vfs_ext: decompress file - not a file");
        return -22;
    }

    decomp_buf = (uint8_t *)kmalloc(src_node->size * 10 + 1024);
    if (!decomp_buf) {
        klog_err("vfs_ext: decompress file - memory allocation failed");
        return -12;
    }

    ret = vfs_ext_decompress_buffer((const uint8_t *)src_node->fs_data, src_node->size,
                                     decomp_buf, src_node->size * 10 + 1024,
                                     algo, &decomp_len);
    if (ret != 0) {
        kfree(decomp_buf);
        return ret;
    }

    if (dst_node->fs_data) {
        kfree(dst_node->fs_data);
    }
    dst_node->fs_data = kmalloc(decomp_len);
    if (!dst_node->fs_data && decomp_len > 0) {
        kfree(decomp_buf);
        return -12;
    }
    if (decomp_len > 0) {
        memcpy(dst_node->fs_data, decomp_buf, decomp_len);
    }
    dst_node->size = decomp_len;

    kfree(decomp_buf);
    *decompressed_size = decomp_len;
    klog_info("vfs_ext: file decompressed %u -> %u bytes",
        src_node->size, decomp_len);
    return 0;
}