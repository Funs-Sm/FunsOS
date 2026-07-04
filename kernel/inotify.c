#include "inotify.h"
#include "kheap.h"
#include "spinlock.h"
#include "sync.h"
#include "sched.h"
#include "string.h"
#include "../drivers/keyboard.h"

static inotify_instance_t inotify_instances[INOTIFY_MAX_INSTANCES];
static int inotify_next_id = 0;
static spinlock_t inotify_global_lock;

void inotify_init(void) {
    for (int i = 0; i < INOTIFY_MAX_INSTANCES; i++) {
        inotify_instances[i].id = -1;
        inotify_instances[i].used = 0;
        inotify_instances[i].watch_count = 0;
        inotify_instances[i].watch_list = NULL;
        inotify_instances[i].event_head = NULL;
        inotify_instances[i].event_tail = NULL;
        inotify_instances[i].event_count = 0;
        inotify_instances[i].max_events = INOTIFY_MAX_EVENTS;
        inotify_instances[i].next_wd = 1;
        spinlock_init(&inotify_instances[i].lock);
        sem_init(&inotify_instances[i].event_sem, 0);
    }
    spinlock_init(&inotify_global_lock);
    inotify_next_id = 100;
}

static inotify_instance_t *inotify_find_instance(int fd) {
    for (int i = 0; i < INOTIFY_MAX_INSTANCES; i++) {
        if (inotify_instances[i].used && inotify_instances[i].id == fd) {
            return &inotify_instances[i];
        }
    }
    return NULL;
}

int inotify_create(int flags) {
    (void)flags;
    spinlock_lock(&inotify_global_lock);

    int idx = -1;
    for (int i = 0; i < INOTIFY_MAX_INSTANCES; i++) {
        if (!inotify_instances[i].used) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        spinlock_unlock(&inotify_global_lock);
        return -24;
    }

    inotify_instances[idx].id = inotify_next_id++;
    inotify_instances[idx].used = 1;
    inotify_instances[idx].watch_count = 0;
    inotify_instances[idx].watch_list = NULL;
    inotify_instances[idx].event_head = NULL;
    inotify_instances[idx].event_tail = NULL;
    inotify_instances[idx].event_count = 0;
    inotify_instances[idx].next_wd = 1;

    int fd = inotify_instances[idx].id;
    spinlock_unlock(&inotify_global_lock);
    return fd;
}

static inotify_watch_t *inotify_find_watch_by_path(inotify_instance_t *inst,
                                                    const char *path) {
    inotify_watch_t *w = inst->watch_list;
    while (w) {
        if (strcmp(w->path, path) == 0) return w;
        w = w->next;
    }
    return NULL;
}

static inotify_watch_t *inotify_find_watch_by_wd(inotify_instance_t *inst, int wd) {
    inotify_watch_t *w = inst->watch_list;
    while (w) {
        if (w->wd == wd) return w;
        w = w->next;
    }
    return NULL;
}

int inotify_add_watch(int fd, const char *path, uint32_t mask) {
    if (!path || !*path) return -22;

    inotify_instance_t *inst = inotify_find_instance(fd);
    if (!inst) return -9;

    spinlock_lock(&inst->lock);

    inotify_watch_t *existing = inotify_find_watch_by_path(inst, path);
    if (existing) {
        if (mask & IN_MASK_ADD) {
            existing->mask |= mask;
        } else {
            existing->mask = mask;
        }
        existing->oneshot = (mask & IN_ONESHOT) ? 1 : 0;
        existing->active = 1;
        int wd = existing->wd;
        spinlock_unlock(&inst->lock);
        return wd;
    }

    if (inst->watch_count >= INOTIFY_MAX_WATCHES) {
        spinlock_unlock(&inst->lock);
        return -28;
    }

    inotify_watch_t *w = (inotify_watch_t *)kmalloc(sizeof(inotify_watch_t));
    if (!w) {
        spinlock_unlock(&inst->lock);
        return -12;
    }

    memset(w, 0, sizeof(*w));
    w->wd = inst->next_wd++;
    strncpy(w->path, path, sizeof(w->path) - 1);
    w->mask = mask;
    w->active = 1;
    w->oneshot = (mask & IN_ONESHOT) ? 1 : 0;
    w->instance = inst;

    w->next = inst->watch_list;
    w->prev = NULL;
    if (inst->watch_list) {
        inst->watch_list->prev = w;
    }
    inst->watch_list = w;
    inst->watch_count++;

    int wd = w->wd;
    spinlock_unlock(&inst->lock);
    return wd;
}

int inotify_rm_watch(int fd, int wd) {
    inotify_instance_t *inst = inotify_find_instance(fd);
    if (!inst) return -9;

    spinlock_lock(&inst->lock);

    inotify_watch_t *w = inotify_find_watch_by_wd(inst, wd);
    if (!w) {
        spinlock_unlock(&inst->lock);
        return -22;
    }

    if (w->prev) {
        w->prev->next = w->next;
    } else {
        inst->watch_list = w->next;
    }
    if (w->next) {
        w->next->prev = w->prev;
    }
    inst->watch_count--;
    kfree(w);

    spinlock_unlock(&inst->lock);
    return 0;
}

static void inotify_queue_event(inotify_instance_t *inst,
                                 inotify_event_t *event) {
    if (inst->event_count >= inst->max_events) {
        uint32_t overflow_event_size = sizeof(inotify_event_t);
        inotify_event_t *ovf = (inotify_event_t *)kmalloc(overflow_event_size);
        if (ovf) {
            memset(ovf, 0, overflow_event_size);
            ovf->wd = -1;
            ovf->mask = IN_Q_OVERFLOW;
            ovf->len = 0;
            inotify_event_node_t *node =
                (inotify_event_node_t *)kmalloc(sizeof(inotify_event_node_t));
            if (node) {
                node->event = ovf;
                node->next = NULL;
                if (inst->event_tail) {
                    inst->event_tail->next = node;
                } else {
                    inst->event_head = node;
                }
                inst->event_tail = node;
                inst->event_count++;
                sem_post(&inst->event_sem);
            } else {
                kfree(ovf);
            }
        }
        return;
    }

    inotify_event_node_t *node =
        (inotify_event_node_t *)kmalloc(sizeof(inotify_event_node_t));
    if (!node) return;

    node->event = event;
    node->next = NULL;

    if (inst->event_tail) {
        inst->event_tail->next = node;
    } else {
        inst->event_head = node;
    }
    inst->event_tail = node;
    inst->event_count++;
    sem_post(&inst->event_sem);
}

int inotify_read(int fd, void *buf, uint32_t count) {
    if (!buf || count == 0) return -22;

    inotify_instance_t *inst = inotify_find_instance(fd);
    if (!inst) return -9;

    uint32_t total_read = 0;
    char *ptr = (char *)buf;

    while (total_read < count) {
        spinlock_lock(&inst->lock);
        if (!inst->event_head) {
            spinlock_unlock(&inst->lock);
            break;
        }

        inotify_event_node_t *node = inst->event_head;
        uint32_t event_size = sizeof(inotify_event_t) + node->event->len;

        if (total_read + event_size > count) {
            spinlock_unlock(&inst->lock);
            if (total_read == 0) return -22;
            break;
        }

        memcpy(ptr, node->event, event_size);
        ptr += event_size;
        total_read += event_size;

        inst->event_head = node->next;
        if (!inst->event_head) {
            inst->event_tail = NULL;
        }
        inst->event_count--;

        kfree(node->event);
        kfree(node);
        spinlock_unlock(&inst->lock);
    }

    if (total_read == 0) {
        return -11;
    }
    return total_read;
}

void inotify_dispatch_event(const char *path, uint32_t mask,
                            const char *name, uint32_t cookie) {
    if (!path) return;

    uint32_t name_len = name ? strlen(name) + 1 : 0;
    uint32_t event_size = sizeof(inotify_event_t) + name_len;

    for (int i = 0; i < INOTIFY_MAX_INSTANCES; i++) {
        if (!inotify_instances[i].used) continue;

        spinlock_lock(&inotify_instances[i].lock);

        inotify_watch_t *w = inotify_instances[i].watch_list;
        while (w) {
            int match = 0;
            if (strcmp(w->path, path) == 0) {
                match = 1;
            } else {
                size_t plen = strlen(w->path);
                if (strncmp(path, w->path, plen) == 0 &&
                    (path[plen] == '/' || path[plen] == '\0')) {
                    if (mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)) {
                        match = 1;
                    }
                }
            }

            if (match && w->active && (w->mask & mask)) {
                inotify_event_t *event = (inotify_event_t *)kmalloc(event_size);
                if (event) {
                    memset(event, 0, event_size);
                    event->wd = w->wd;
                    event->mask = mask;
                    event->cookie = cookie;
                    event->len = name_len;
                    if (name && name_len > 0) {
                        strcpy(event->name, name);
                    }
                    inotify_queue_event(&inotify_instances[i], event);
                }

                if (w->oneshot) {
                    w->active = 0;
                }
            }
            w = w->next;
        }

        spinlock_unlock(&inotify_instances[i].lock);
    }
}

int sys_inotify_init(void) {
    return inotify_create(0);
}

int sys_inotify_init1(int flags) {
    return inotify_create(flags);
}

int sys_inotify_add_watch(int fd, const char *path, uint32_t mask) {
    return inotify_add_watch(fd, path, mask);
}

int sys_inotify_rm_watch(int fd, int wd) {
    return inotify_rm_watch(fd, wd);
}

void inotify_dump(int fd) {
    inotify_instance_t *inst = inotify_find_instance(fd);
    if (!inst) return;

    spinlock_lock(&inst->lock);
    inotify_watch_t *w = inst->watch_list;
    while (w) {
        w = w->next;
    }
    spinlock_unlock(&inst->lock);
}

int inotify_get_instance_count(void) {
    int count = 0;
    for (int i = 0; i < INOTIFY_MAX_INSTANCES; i++) {
        if (inotify_instances[i].used) count++;
    }
    return count;
}
