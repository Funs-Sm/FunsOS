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