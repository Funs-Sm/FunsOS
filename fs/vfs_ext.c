#include "vfs.h"
#include "vfs_ext.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

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

static vfs_ext_aio_request_t g_aio_requests[VFS_EXT_AIO_MAX_REQUESTS];
static uint32_t g_aio_next_id = 1;
static int g_aio_initialized = 0;

int vfs_ext_aio_init(void)
{
    memset(g_aio_requests, 0, sizeof(g_aio_requests));
    g_aio_next_id = 1;
    g_aio_initialized = 1;
    klog_info("vfs_ext: aio system initialized");
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
    uint32_t i;
    if (!g_aio_initialized) return -1;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active && g_aio_requests[i].request_id == req_id) {
            if (g_aio_requests[i].type == VFS_EXT_AIO_READ) {
                g_aio_requests[i].result = vfs_ext_cache_read(g_aio_requests[i].inode, g_aio_requests[i].offset, g_aio_requests[i].buffer, g_aio_requests[i].size);
            } else if (g_aio_requests[i].type == VFS_EXT_AIO_WRITE) {
                g_aio_requests[i].result = vfs_ext_cache_write(g_aio_requests[i].inode, g_aio_requests[i].offset, g_aio_requests[i].buffer, g_aio_requests[i].size);
            }
            g_aio_requests[i].state = (g_aio_requests[i].result >= 0) ? VFS_EXT_AIO_STATE_DONE : VFS_EXT_AIO_STATE_ERROR;
            g_aio_requests[i].completed = 0;

            if (g_aio_requests[i].callback) {
                g_aio_requests[i].callback(g_aio_requests[i].result, g_aio_requests[i].user_data);
            }
            return g_aio_requests[i].result;
        }
    }
    return -1;
}

int vfs_ext_aio_poll(uint32_t req_id, int *done, int *result)
{
    uint32_t i;
    if (!g_aio_initialized || !done || !result) return -1;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active && g_aio_requests[i].request_id == req_id) {
            if (g_aio_requests[i].state == VFS_EXT_AIO_STATE_DONE || g_aio_requests[i].state == VFS_EXT_AIO_STATE_ERROR) {
                *done = 1;
                *result = g_aio_requests[i].result;
            } else {
                *done = 0;
                *result = 0;
            }
            return 0;
        }
    }
    return -1;
}

int vfs_ext_aio_cancel(uint32_t req_id)
{
    uint32_t i;
    if (!g_aio_initialized) return -1;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active && g_aio_requests[i].request_id == req_id) {
            if (g_aio_requests[i].state == VFS_EXT_AIO_STATE_PENDING || g_aio_requests[i].state == VFS_EXT_AIO_STATE_RUNNING) {
                g_aio_requests[i].active = 0;
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

int vfs_ext_aio_process_all(void)
{
    uint32_t i;
    int processed = 0;
    if (!g_aio_initialized) return 0;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active && g_aio_requests[i].state == VFS_EXT_AIO_STATE_PENDING) {
            g_aio_requests[i].state = VFS_EXT_AIO_STATE_RUNNING;

            if (g_aio_requests[i].type == VFS_EXT_AIO_READ) {
                g_aio_requests[i].result = vfs_ext_cache_read(g_aio_requests[i].inode, g_aio_requests[i].offset, g_aio_requests[i].buffer, g_aio_requests[i].size);
            } else if (g_aio_requests[i].type == VFS_EXT_AIO_WRITE) {
                g_aio_requests[i].result = vfs_ext_cache_write(g_aio_requests[i].inode, g_aio_requests[i].offset, g_aio_requests[i].buffer, g_aio_requests[i].size);
            }

            g_aio_requests[i].state = (g_aio_requests[i].result >= 0) ? VFS_EXT_AIO_STATE_DONE : VFS_EXT_AIO_STATE_ERROR;
            g_aio_requests[i].completed = 0;

            if (g_aio_requests[i].callback) {
                g_aio_requests[i].callback(g_aio_requests[i].result, g_aio_requests[i].user_data);
            }
            processed++;
        }
    }
    return processed;
}

int vfs_ext_aio_cleanup(void)
{
    uint32_t i;
    int cleaned = 0;
    if (!g_aio_initialized) return 0;

    for (i = 0; i < VFS_EXT_AIO_MAX_REQUESTS; i++) {
        if (g_aio_requests[i].active && (g_aio_requests[i].state == VFS_EXT_AIO_STATE_DONE || g_aio_requests[i].state == VFS_EXT_AIO_STATE_ERROR)) {
            g_aio_requests[i].active = 0;
            cleaned++;
        }
    }
    return cleaned;
}