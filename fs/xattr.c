/*
 * fs/xattr.c - extended attribute storage.
 *
 * xattrs live on a small dynamic array stored as the inode's
 * private_data pointer (only if it has xattrs - otherwise the slot
 * stays free for the FS backend).  We use this layout because the
 * VFS superblock backend layout varies (ext*, ramfs, btrfs) and we
 * don't want to bake xattrs into the inode itself.
 *
 * Limits: per-inode count 32, name 64 bytes, value 4 KiB.  These match
 * Linux defaults for non-trusted namespaces.
 */
#include "xattr.h"
#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "errno.h"

typedef struct xattr_inode {
    xattr_entry_t entries[XATTR_MAX_PER_INODE];
    uint32_t      count;
} xattr_inode_t;

static xattr_stats_t g_stats;

static xattr_ns_t parse_namespace(const char *name, const char **rest)
{
    if (!name || !rest) return XATTR_NS_UNKNOWN;
    if (strncmp(name, "user.", 5) == 0) {
        *rest = name + 5;
        return XATTR_NS_USER;
    }
    if (strncmp(name, "system.", 7) == 0) {
        *rest = name + 7;
        return XATTR_NS_SYSTEM;
    }
    if (strncmp(name, "trusted.", 8) == 0) {
        *rest = name + 8;
        return XATTR_NS_TRUSTED;
    }
    if (strncmp(name, "security.", 9) == 0) {
        *rest = name + 9;
        return XATTR_NS_SECURITY;
    }
    *rest = name;
    return XATTR_NS_UNKNOWN;
}

static xattr_inode_t *get_or_create(struct inode_t *inode)
{
    if (!inode) return NULL;
    if (inode->private_data) return (xattr_inode_t *)inode->private_data;
    /* Use private_data slot only if it's free (NULL).  If a backend has
     * already claimed it, we cannot host xattrs for this inode. */
    xattr_inode_t *xi = (xattr_inode_t *)kmalloc(sizeof(xattr_inode_t));
    if (!xi) return NULL;
    memset(xi, 0, sizeof(*xi));
    inode->private_data = xi;
    return xi;
}

static xattr_entry_t *find(xattr_inode_t *xi, const char *name)
{
    if (!xi || !name) return NULL;
    for (uint32_t i = 0; i < xi->count; i++) {
        if (strcmp(xi->entries[i].name, name) == 0) return &xi->entries[i];
    }
    return NULL;
}

int xattr_set(struct inode_t *inode, const char *name, const void *value,
              uint32_t value_len, int flags)
{
    g_stats.sets++;
    if (!inode || !name || !value) return -EINVAL;
    if (value_len > XATTR_VALUE_MAX) return -ENOSPC;
    uint32_t nlen = 0;
    while (name[nlen]) nlen++;
    if (nlen == 0 || nlen >= XATTR_NAME_MAX) return -EINVAL;

    const char *unprefixed = NULL;
    xattr_ns_t ns = parse_namespace(name, &unprefixed);
    if (ns == XATTR_NS_UNKNOWN) return -EINVAL;

    xattr_inode_t *xi = get_or_create(inode);
    if (!xi) return -ENOMEM;

    xattr_entry_t *e = find(xi, unprefixed);
    if (e) {
        /* Replace. */
        if (e->value) kfree(e->value);
        e->value = (uint8_t *)kmalloc(value_len);
        if (!e->value) return -ENOMEM;
        memcpy(e->value, value, value_len);
        e->value_len = value_len;
        return 0;
    }

    if (xi->count >= XATTR_MAX_PER_INODE) return -ENOSPC;
    if (flags == 1 /* XATTR_CREATE */) return -EEXIST;
    if (flags == 2 /* XATTR_REPLACE */) return -ENODATA;

    e = &xi->entries[xi->count++];
    uint32_t cp = 0;
    while (unprefixed[cp] && cp < XATTR_NAME_MAX - 1) {
        e->name[cp] = unprefixed[cp];
        cp++;
    }
    e->name[cp] = '\0';
    e->ns = (uint8_t)ns;
    e->value_len = value_len;
    e->value = (uint8_t *)kmalloc(value_len);
    if (!e->value) { xi->count--; return -ENOMEM; }
    memcpy(e->value, value, value_len);
    return 0;
}

int xattr_get(struct inode_t *inode, const char *name, void *buf, uint32_t buf_size)
{
    g_stats.gets++;
    if (!inode || !name || !buf) return -EINVAL;
    const char *unprefixed = NULL;
    xattr_ns_t ns = parse_namespace(name, &unprefixed);
    if (ns == XATTR_NS_UNKNOWN) return -EINVAL;

    xattr_inode_t *xi = (xattr_inode_t *)inode->private_data;
    if (!xi) { g_stats.misses++; return -ENODATA; }
    xattr_entry_t *e = find(xi, unprefixed);
    if (!e) { g_stats.misses++; return -ENODATA; }
    if (e->value_len > buf_size) return -ERANGE;
    memcpy(buf, e->value, e->value_len);
    return (int)e->value_len;
}

int xattr_list(struct inode_t *inode, char *buf, uint32_t buf_size)
{
    g_stats.lists++;
    if (!inode || !buf || buf_size == 0) return -EINVAL;
    xattr_inode_t *xi = (xattr_inode_t *)inode->private_data;
    if (!xi) return 0;
    uint32_t off = 0;
    static const char *prefix[4] = { "user.", "system.", "trusted.", "security." };
    for (uint32_t i = 0; i < xi->count; i++) {
        if (xi->entries[i].ns > 3) continue;
        const char *p = prefix[xi->entries[i].ns];
        uint32_t pl = 0;
        while (p[pl]) pl++;
        uint32_t nl = 0;
        while (xi->entries[i].name[nl]) nl++;
        if (off + pl + nl + 1 >= buf_size) return -ERANGE;
        memcpy(buf + off, p, pl); off += pl;
        memcpy(buf + off, xi->entries[i].name, nl); off += nl;
        buf[off++] = '\0';
    }
    return (int)off;
}

int xattr_remove(struct inode_t *inode, const char *name)
{
    g_stats.removes++;
    if (!inode || !name) return -EINVAL;
    const char *unprefixed = NULL;
    xattr_ns_t ns = parse_namespace(name, &unprefixed);
    if (ns == XATTR_NS_UNKNOWN) return -EINVAL;

    xattr_inode_t *xi = (xattr_inode_t *)inode->private_data;
    if (!xi) return -ENODATA;
    for (uint32_t i = 0; i < xi->count; i++) {
        if (strcmp(xi->entries[i].name, unprefixed) == 0) {
            if (xi->entries[i].value) kfree(xi->entries[i].value);
            /* Compact. */
            for (uint32_t j = i; j + 1 < xi->count; j++) {
                xi->entries[j] = xi->entries[j + 1];
            }
            xi->count--;
            memset(&xi->entries[xi->count], 0, sizeof(xattr_entry_t));
            return 0;
        }
    }
    return -ENODATA;
}

void xattr_get_stats(xattr_stats_t *out)
{
    if (!out) return;
    out->sets    = g_stats.sets;
    out->gets    = g_stats.gets;
    out->lists   = g_stats.lists;
    out->removes = g_stats.removes;
    out->misses  = g_stats.misses;
}

void xattr_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}
