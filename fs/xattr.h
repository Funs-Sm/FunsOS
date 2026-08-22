/*
 * fs/xattr.h - extended attribute (key/value) storage attached to
 * FunsCore inodes.
 *
 * The backing storage is a small dynamic array on the inode itself
 * (`inode->xattrs`); the array is sized to grow on demand.  xattr is
 * mostly used by:
 *   - SELinux/AppArmor: security.selinux / security.apparmor labels
 *   - ACL subsystem:    system.posix_acl_access
 *   - ACL on overlayfs: trusted.overlay.opaque
 *   - User namespace:   system.nfs4_acl
 *   - Quota subsystem:  funsos.quota.user_bytes
 *
 * Each attribute is a {ns, name, value} triple.  Namespace prefix is
 * one of:
 *   "user."    -> user.*  (subject to user_xattr mount option)
 *   "system."  -> system.*  (always allowed)
 *   "trusted." -> trusted.*  (only CAP_SYS_ADMIN)
 *   "security." -> security.*  (always allowed)
 * No other prefix is valid.
 */
#ifndef FS_XATTR_H
#define FS_XATTR_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "vfs.h"

/* Forward decl: the inode struct lives in vfs.h. */
struct inode_t;

#define XATTR_NAME_MAX 64
#define XATTR_VALUE_MAX 4096
#define XATTR_MAX_PER_INODE 32

typedef enum {
    XATTR_NS_USER     = 0,
    XATTR_NS_SYSTEM   = 1,
    XATTR_NS_TRUSTED  = 2,
    XATTR_NS_SECURITY = 3,
    XATTR_NS_UNKNOWN  = 0xFF
} xattr_ns_t;

typedef struct xattr_entry {
    char       name[XATTR_NAME_MAX];
    uint32_t   value_len;
    uint8_t    ns;       /* xattr_ns_t */
    uint8_t   *value;
} xattr_entry_t;

/* Return 0 on success, -ENOSPC / -EINVAL / -ENODATA otherwise. */
int  xattr_set(struct inode_t *inode, const char *name, const void *value,
               uint32_t value_len, int flags);
int  xattr_get(struct inode_t *inode, const char *name, void *buf, uint32_t buf_size);
int  xattr_list(struct inode_t *inode, char *buf, uint32_t buf_size);
int  xattr_remove(struct inode_t *inode, const char *name);

/* Statistics (debugfs-style readout via `cmd_xattr`). */
typedef struct xattr_stats {
    uint64_t sets;
    uint64_t gets;
    uint64_t lists;
    uint64_t removes;
    uint64_t misses;
} xattr_stats_t;

void xattr_get_stats(xattr_stats_t *out);
void xattr_reset_stats(void);

#endif /* FS_XATTR_H */
