#ifndef DENTRY_H
#define DENTRY_H

#include "vfs.h"

#define DENTRY_CACHE_SIZE 1024

void dentry_cache_init(void);
dentry_t *dentry_alloc(const char *name, inode_t *inode, dentry_t *parent);
void dentry_free(dentry_t *d);
dentry_t *dentry_lookup(dentry_t *parent, const char *name);
void dentry_add_child(dentry_t *parent, dentry_t *child);
void dentry_remove_child(dentry_t *parent, const char *name);
void dentry_cache_shrink(void);

#endif
