#include "path.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"

extern dentry_t *root_dentry;
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

int32_t path_resolve(const char *path, dentry_t **out) {
    if (!path || !out) return -1;

    char normalized[PATH_MAX];
    if (path_normalize(path, normalized, PATH_MAX) != 0) return -1;

    dentry_t *current = root_dentry;

    if (normalized[0] != '/') return -1;

    uint32_t i = 1;
    while (normalized[i]) {
        while (normalized[i] == '/') i++;
        if (!normalized[i]) break;

        uint32_t start = i;
        while (normalized[i] && normalized[i] != '/') i++;
        uint32_t comp_len = i - start;

        char component[256];
        if (comp_len >= 256) return -1;
        memcpy(component, normalized + start, comp_len);
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
