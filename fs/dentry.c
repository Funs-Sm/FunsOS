#include "dentry.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"

static dentry_t *cache_lru_head;
static dentry_t *cache_lru_tail;
static uint32_t cache_count;

void dentry_cache_init(void) {
    cache_lru_head = NULL;
    cache_lru_tail = NULL;
    cache_count = 0;
}

/* Add `d` to the front (MRU position) of the LRU list. */
static void cache_attach(dentry_t *d) {
    d->cache_prev = NULL;
    d->cache_next = cache_lru_head;
    if (cache_lru_head) {
        cache_lru_head->cache_prev = d;
    } else {
        cache_lru_tail = d;
    }
    cache_lru_head = d;
    cache_count++;
}

/* Remove `d` from the LRU list. */
static void cache_detach(dentry_t *d) {
    if (d->cache_prev) {
        d->cache_prev->cache_next = d->cache_next;
    } else {
        cache_lru_head = d->cache_next;
    }
    if (d->cache_next) {
        d->cache_next->cache_prev = d->cache_prev;
    } else {
        cache_lru_tail = d->cache_prev;
    }
    d->cache_prev = NULL;
    d->cache_next = NULL;
    cache_count--;
}

dentry_t *dentry_alloc(const char *name, inode_t *inode, dentry_t *parent) {
    dentry_t *d = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!d) return NULL;

    memset(d, 0, sizeof(dentry_t));
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->inode = inode;
    d->parent = parent;

    if (parent) {
        dentry_add_child(parent, d);
    }

    cache_attach(d);

    if (cache_count > DENTRY_CACHE_SIZE) {
        dentry_cache_shrink();
    }

    return d;
}

void dentry_free(dentry_t *d) {
    if (!d) return;

    if (d->parent) {
        dentry_remove_child(d->parent, d->name);
    }

    cache_detach(d);
    kfree(d);
}

dentry_t *dentry_lookup(dentry_t *parent, const char *name) {
    if (!parent || !name) return NULL;

    dentry_t *child = parent->child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }

    return NULL;
}

void dentry_add_child(dentry_t *parent, dentry_t *child) {
    if (!parent || !child) return;

    child->next_sibling = parent->child;
    parent->child = child;
    child->parent = parent;
}

void dentry_remove_child(dentry_t *parent, const char *name) {
    if (!parent || !name) return;

    dentry_t *prev = NULL;
    dentry_t *curr = parent->child;

    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (prev) {
                prev->next_sibling = curr->next_sibling;
            } else {
                parent->child = curr->next_sibling;
            }
            curr->parent = NULL;
            return;
        }
        prev = curr;
        curr = curr->next_sibling;
    }
}

void dentry_cache_shrink(void) {
    while (cache_count > DENTRY_CACHE_SIZE && cache_lru_tail) {
        dentry_t *victim = cache_lru_tail;

        cache_detach(victim);

        if (victim->parent) {
            dentry_remove_child(victim->parent, victim->name);
        }

        kfree(victim);
    }
}
