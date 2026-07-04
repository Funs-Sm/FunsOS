#include "epoll.h"
#include "kheap.h"
#include "spinlock.h"
#include "sync.h"
#include "sched.h"
#include "string.h"
#include "../drivers/keyboard.h"

static epoll_instance_t epoll_instances[EPOLL_MAX_INSTANCES];
static int epoll_next_id = 0;
static spinlock_t epoll_global_lock;

void epoll_init(void) {
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        epoll_instances[i].id = -1;
        epoll_instances[i].used = 0;
        epoll_instances[i].size = 0;
        epoll_instances[i].fd_count = 0;
        epoll_instances[i].fd_list = NULL;
        epoll_instances[i].ready_list = NULL;
        epoll_instances[i].ready_count = 0;
        epoll_instances[i].closed = 0;
        spinlock_init(&epoll_instances[i].lock);
        sem_init(&epoll_instances[i].wait_sem, 0);
    }
    spinlock_init(&epoll_global_lock);
    epoll_next_id = 0;
}

static epoll_instance_t *epoll_find_instance(int epfd) {
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (epoll_instances[i].used && epoll_instances[i].id == epfd) {
            return &epoll_instances[i];
        }
    }
    return NULL;
}

int epoll_create(int size) {
    if (size <= 0) size = EPOLL_MAX_FDS;
    return epoll_create1(0);
}

int epoll_create1(int flags) {
    (void)flags;
    spinlock_lock(&epoll_global_lock);

    int idx = -1;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!epoll_instances[i].used) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        spinlock_unlock(&epoll_global_lock);
        return -24;
    }

    epoll_instances[idx].id = epoll_next_id++;
    epoll_instances[idx].used = 1;
    epoll_instances[idx].size = EPOLL_MAX_FDS;
    epoll_instances[idx].fd_count = 0;
    epoll_instances[idx].fd_list = NULL;
    epoll_instances[idx].ready_list = NULL;
    epoll_instances[idx].ready_count = 0;
    epoll_instances[idx].closed = 0;

    int epfd = epoll_instances[idx].id;
    spinlock_unlock(&epoll_global_lock);
    return epfd;
}

static epoll_fd_entry_t *epoll_find_fd(epoll_instance_t *ep, int fd) {
    epoll_fd_entry_t *entry = ep->fd_list;
    while (entry) {
        if (entry->fd == fd) return entry;
        entry = entry->next;
    }
    return NULL;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    if (fd < 0) return -9;

    epoll_instance_t *ep = epoll_find_instance(epfd);
    if (!ep) return -9;

    spinlock_lock(&ep->lock);

    epoll_fd_entry_t *entry = epoll_find_fd(ep, fd);

    switch (op) {
        case EPOLL_CTL_ADD:
            if (entry) {
                spinlock_unlock(&ep->lock);
                return -17;
            }
            if (ep->fd_count >= EPOLL_MAX_FDS) {
                spinlock_unlock(&ep->lock);
                return -24;
            }
            entry = (epoll_fd_entry_t *)kmalloc(sizeof(epoll_fd_entry_t));
            if (!entry) {
                spinlock_unlock(&ep->lock);
                return -12;
            }
            memset(entry, 0, sizeof(*entry));
            entry->fd = fd;
            entry->events = event ? event->events : 0;
            entry->data = event ? event->data : 0;
            entry->revents = 0;
            entry->active = 1;
            entry->oneshot = (entry->events & EPOLLONESHOT) ? 1 : 0;
            entry->et = (entry->events & EPOLLET) ? 1 : 0;
            entry->next = ep->fd_list;
            entry->prev = NULL;
            if (ep->fd_list) {
                ep->fd_list->prev = entry;
            }
            ep->fd_list = entry;
            ep->fd_count++;
            break;

        case EPOLL_CTL_DEL:
            if (!entry) {
                spinlock_unlock(&ep->lock);
                return -2;
            }
            if (entry->prev) {
                entry->prev->next = entry->next;
            } else {
                ep->fd_list = entry->next;
            }
            if (entry->next) {
                entry->next->prev = entry->prev;
            }
            ep->fd_count--;
            kfree(entry);
            break;

        case EPOLL_CTL_MOD:
            if (!entry) {
                spinlock_unlock(&ep->lock);
                return -2;
            }
            if (event) {
                entry->events = event->events;
                entry->data = event->data;
                entry->oneshot = (entry->events & EPOLLONESHOT) ? 1 : 0;
                entry->et = (entry->events & EPOLLET) ? 1 : 0;
            }
            break;

        default:
            spinlock_unlock(&ep->lock);
            return -22;
    }

    spinlock_unlock(&ep->lock);
    return 0;
}

static int epoll_collect_ready(epoll_instance_t *ep,
                                struct epoll_event *events,
                                int maxevents) {
    int count = 0;
    epoll_fd_entry_t *entry = ep->fd_list;

    while (entry && count < maxevents) {
        if (entry->active && entry->revents) {
            if (events) {
                events[count].events = entry->revents;
                events[count].data = entry->data;
            }
            count++;

            if (entry->oneshot) {
                entry->active = 0;
            }
            if (!entry->et) {
                entry->revents = 0;
            }
        }
        entry = entry->next;
    }

    return count;
}

int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout) {
    if (!events || maxevents <= 0) return -22;

    epoll_instance_t *ep = epoll_find_instance(epfd);
    if (!ep) return -9;

    int count = 0;
    uint64_t start = sched_get_tick_count();

    while (1) {
        spinlock_lock(&ep->lock);
        count = epoll_collect_ready(ep, events, maxevents);
        spinlock_unlock(&ep->lock);

        if (count > 0) break;
        if (timeout == 0) break;
        if (kb_signal_check()) return -4;
        if (timeout > 0) {
            if ((uint32_t)(sched_get_tick_count() - start) >= (uint32_t)timeout) {
                break;
            }
        }
        sched_sleep(10);
    }

    return count;
}

void epoll_notify(int fd, uint32_t events) {
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!epoll_instances[i].used) continue;

        spinlock_lock(&epoll_instances[i].lock);

        epoll_fd_entry_t *entry = epoll_find_fd(&epoll_instances[i], fd);
        if (entry && entry->active) {
            entry->revents |= (events & entry->events);
            if (entry->revents) {
                sem_post(&epoll_instances[i].wait_sem);
            }
        }

        spinlock_unlock(&epoll_instances[i].lock);
    }
}

int sys_epoll_create(int size) {
    return epoll_create(size);
}

int sys_epoll_create1(int flags) {
    return epoll_create1(flags);
}

int sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    return epoll_ctl(epfd, op, fd, event);
}

int sys_epoll_wait(int epfd, struct epoll_event *events,
                   int maxevents, int timeout) {
    return epoll_wait(epfd, events, maxevents, timeout);
}

void epoll_dump(int epfd) {
    epoll_instance_t *ep = epoll_find_instance(epfd);
    if (!ep) return;

    spinlock_lock(&ep->lock);
    epoll_fd_entry_t *entry = ep->fd_list;
    while (entry) {
        entry = entry->next;
    }
    spinlock_unlock(&ep->lock);
}

int epoll_get_count(void) {
    int count = 0;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (epoll_instances[i].used) count++;
    }
    return count;
}
