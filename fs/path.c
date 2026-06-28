#include "path.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"

extern dentry_t *root_dentry;
extern dentry_t *cwd_dentry;
extern mount_t *mount_list;

int32_t path_normalize(const char *path, char *out, uint32_t out_size) {
    if (!path || !out || out_size == 0) return -1;

    uint32_t i = 0, o = 0;
    int32_t components_start[PATH_MAX / 2];
    int32_t components_len[PATH_MAX / 2];
    int comp_count = 0;

    int is_abs = (path[0] == '/');
    if (is_abs) {
        out[o++] = '/';
        i++;
    }

    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;

        uint32_t start = i;
        while (path[i] && path[i] != '/') i++;
        uint32_t len = i - start;

        if (len == 1 && path[start] == '.') continue;
        if (len == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (comp_count > 0) {
                comp_count--;
            }
            continue;
        }

        components_start[comp_count] = start;
        components_len[comp_count] = len;
        comp_count++;
    }

    o = 0;
    if (is_abs) {
        out[o++] = '/';
    }

    for (int c = 0; c < comp_count; c++) {
        if (c > 0 && !(o == 1 && is_abs)) {
            if (o + 1 >= out_size) return -1;
            out[o++] = '/';
        } else if (c > 0) {
            if (o + 1 >= out_size) return -1;
            out[o++] = '/';
        }
        for (int32_t j = 0; j < components_len[c]; j++) {
            if (o + 1 >= out_size) return -1;
            out[o++] = path[components_start[c] + j];
        }
    }

    if (o == 0) {
        out[o++] = '.';
    }

    out[o] = '\0';
    return 0;
}

int32_t path_parent(const char *path, char *out, uint32_t out_size) {
    if (!path || !out || out_size == 0) return -1;

    int32_t len = 0;
    while (path[len]) len++;

    int32_t end = len - 1;
    while (end >= 0 && path[end] == '/') end--;

    if (end < 0) {
        if (out_size < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    while (end >= 0 && path[end] != '/') end--;

    while (end >= 0 && path[end] == '/') end--;

    if (end < 0) {
        if (out_size < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    if ((uint32_t)(end + 1) >= out_size) return -1;
    memcpy(out, path, end + 1);
    out[end + 1] = '\0';
    return 0;
}

int32_t path_basename(const char *path, char *out, uint32_t out_size) {
    if (!path || !out || out_size == 0) return -1;

    int32_t len = 0;
    while (path[len]) len++;

    int32_t end = len - 1;
    while (end >= 0 && path[end] == '/') end--;

    if (end < 0) {
        if (out_size < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    int32_t start = end;
    while (start >= 0 && path[start] != '/') start--;
    start++;

    uint32_t baselen = end - start + 1;
    if (baselen >= out_size) return -1;
    memcpy(out, path + start, baselen);
    out[baselen] = '\0';
    return 0;
}

int32_t path_join(const char *dir, const char *name, char *out, uint32_t out_size) {
    if (!dir || !name || !out || out_size == 0) return -1;

    uint32_t dlen = 0;
    while (dir[dlen]) dlen++;

    while (dlen > 0 && dir[dlen - 1] == '/') dlen--;

    uint32_t nstart = 0;
    while (name[nstart] == '/') nstart++;

    uint32_t nlen = 0;
    while (name[nstart + nlen]) nlen++;

    if (dlen + 1 + nlen + 1 > out_size) return -1;

    memcpy(out, dir, dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1, name + nstart, nlen);
    out[dlen + 1 + nlen] = '\0';
    return 0;
}

/* Build the absolute path of a dentry into `out`.  Used when following
 * relative symbolic links. */
static int32_t dentry_path(dentry_t *d, char *out, uint32_t out_size) {
    if (!d || !out || out_size == 0) return -1;
    if (d == root_dentry) {
        if (out_size < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    uint32_t len = 1; /* terminating '\0' */
    dentry_t *p = d;
    while (p && p != root_dentry) {
        uint32_t nlen = 0;
        while (p->name[nlen]) nlen++;
        len += nlen + 1; /* name + '/' */
        p = p->parent;
    }
    if (len > out_size) return -1;

    uint32_t pos = len - 1;
    out[pos] = '\0';
    p = d;
    while (p && p != root_dentry) {
        uint32_t nlen = 0;
        while (p->name[nlen]) nlen++;
        pos -= nlen;
        memcpy(out + pos, p->name, nlen);
        p = p->parent;
        if (p && p != root_dentry) {
            pos--;
            out[pos] = '/';
        }
    }
    return 0;
}

/* Internal path resolver.  `follow_last` controls whether a symbolic
 * link encountered as the final path component is followed. */
static int32_t path_resolve_internal(const char *path, dentry_t **out, int follow_last) {
    if (!path || !out) return -1;

    char work[PATH_MAX];
    if (path_normalize(path, work, PATH_MAX) != 0) return -1;

    int32_t symlink_depth = 0;
    int is_relative = 0;

restart:
    if (symlink_depth > SYMLINK_MAX_FOLLOW) return -1;

    dentry_t *current;
    uint32_t i;
    if (work[0] == '/') {
        current = root_dentry;
        i = 1;
    } else {
        current = cwd_dentry ? cwd_dentry : root_dentry;
        i = 0;
        is_relative = 1;
    }

    while (work[i]) {
        while (work[i] == '/') i++;
        if (!work[i]) break;

        uint32_t start = i;
        while (work[i] && work[i] != '/') i++;
        uint32_t comp_len = i - start;

        char component[256];
        if (comp_len >= 256) return -1;
        memcpy(component, work + start, comp_len);
        component[comp_len] = '\0';

        if (current->mount_point) {
            mount_t *mnt = mount_list;
            while (mnt) {
                if (mnt->mount_point == current) {
                    current = mnt->root_dentry;
                    break;
                }
                mnt = mnt->next;
            }
        }

        dentry_t *child = current->child;
        dentry_t *found = NULL;
        while (child) {
            if (memcmp(child->name, component, comp_len) == 0 && child->name[comp_len] == '\0') {
                found = child;
                break;
            }
            child = child->next_sibling;
        }

        if (!found) {
            if (current->inode && current->inode->ops && current->inode->ops->lookup) {
                found = current->inode->ops->lookup(current, component);
            }
        }

        if (!found) return -1;

        /* Follow symbolic links.  The final component is only followed
         * when follow_last is true (open/stat) and not when it is false
         * (readlink/lstat). */
        int is_last_component = (work[i] == '\0');
        if (found->inode && (found->inode->mode & FILE_MODE_LNK)) {
            if (is_last_component && !follow_last) {
                current = found;
                break;
            }

            if (symlink_depth >= SYMLINK_MAX_FOLLOW) return -1;
            if (!found->inode->ops || !found->inode->ops->readlink) return -1;

            char target[PATH_MAX];
            int32_t tlen = found->inode->ops->readlink(found, target, PATH_MAX - 1);
            if (tlen < 0) return -1;
            target[tlen] = '\0';

            char rest[PATH_MAX];
            uint32_t rlen = 0;
            while (work[i + rlen] && rlen < PATH_MAX - 1) {
                rest[rlen] = work[i + rlen];
                rlen++;
            }
            rest[rlen] = '\0';

            char combined[PATH_MAX];
            if (target[0] == '/') {
                if (path_join(target, rest, combined, PATH_MAX) != 0) return -1;
            } else {
                char parent_path[PATH_MAX];
                if (dentry_path(found->parent ? found->parent : root_dentry,
                                parent_path, PATH_MAX) != 0) {
                    return -1;
                }
                if (path_join(parent_path, target, combined, PATH_MAX) != 0) return -1;
                if (rlen > 0) {
                    if (path_join(combined, rest, combined, PATH_MAX) != 0) return -1;
                }
            }

            if (path_normalize(combined, work, PATH_MAX) != 0) return -1;
            symlink_depth++;
            goto restart;
        }

        current = found;
    }

    if (current->mount_point) {
        mount_t *mnt = mount_list;
        while (mnt) {
            if (mnt->mount_point == current) {
                current = mnt->root_dentry;
                break;
            }
            mnt = mnt->next;
        }
    }

    *out = current;
    return 0;
}

int32_t path_resolve(const char *path, dentry_t **out) {
    return path_resolve_internal(path, out, 1);
}

int32_t path_resolve_nofollow(const char *path, dentry_t **out) {
    return path_resolve_internal(path, out, 0);
}
