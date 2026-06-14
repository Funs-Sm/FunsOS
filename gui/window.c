#include "window.h"
#include "kheap.h"
#include "string.h"

static uint32_t next_window_id = 1;

window_t *window_create(window_t *parent, const char *title, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t flags) {
    window_t *win = (window_t *)kmalloc(sizeof(window_t));
    if (!win) return 0;

    memset(win, 0, sizeof(window_t));
    win->id = next_window_id++;
    if (title) {
        uint32_t i;
        for (i = 0; i < 63 && title[i]; i++) win->title[i] = title[i];
        win->title[i] = '\0';
    }
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->flags = flags;
    win->state = WINDOW_STATE_NORMAL;
    win->focused = 0;
    win->dirty = 1;

    win->context = (gfx_context_t *)kmalloc(sizeof(gfx_context_t));
    if (!win->context) {
        kfree(win);
        return 0;
    }

    uint32_t *buf = (uint32_t *)kmalloc(w * h * 4);
    if (!buf) {
        kfree(win->context);
        kfree(win);
        return 0;
    }
    memset(buf, 0, w * h * 4);
    gfx_init(win->context, buf, w, h, w * 4, 32);

    win->parent = parent;
    win->first_child = 0;
    win->next_sibling = 0;
    win->prev = 0;
    win->next = 0;
    win->event_handler = 0;
    win->user_data = 0;

    if (parent) {
        window_t *sibling = parent->first_child;
        if (!sibling) {
            parent->first_child = win;
        } else {
            while (sibling->next_sibling) sibling = sibling->next_sibling;
            sibling->next_sibling = win;
        }
    }

    return win;
}

void window_destroy(window_t *win) {
    if (!win) return;

    if (win->parent) {
        window_t **pp = &win->parent->first_child;
        while (*pp) {
            if (*pp == win) {
                *pp = win->next_sibling;
                break;
            }
            pp = &(*pp)->next_sibling;
        }
    }

    window_t *child = win->first_child;
    while (child) {
        window_t *next = child->next_sibling;
        child->parent = 0;
        window_destroy(child);
        child = next;
    }

    if (win->context) {
        if (win->context->buffer) kfree(win->context->buffer);
        kfree(win->context);
    }
    kfree(win);
}

void window_show(window_t *win) {
    if (!win) return;
    win->flags |= WINDOW_FLAG_VISIBLE;
    win->dirty = 1;
}

void window_hide(window_t *win) {
    if (!win) return;
    win->flags &= ~WINDOW_FLAG_VISIBLE;
}

void window_move(window_t *win, int32_t x, int32_t y) {
    if (!win) return;
    win->x = x;
    win->y = y;
    win->dirty = 1;
}

void window_resize(window_t *win, int32_t w, int32_t h) {
    if (!win) return;
    if (w <= 0 || h <= 0) return;

    if (win->context) {
        if (win->context->buffer) kfree(win->context->buffer);
        win->context->buffer = (uint32_t *)kmalloc(w * h * 4);
        if (win->context->buffer) {
            memset(win->context->buffer, 0, w * h * 4);
        }
        gfx_init(win->context, win->context->buffer, w, h, w * 4, 32);
    }

    win->width = w;
    win->height = h;
    win->dirty = 1;
}

void window_set_title(window_t *win, const char *title) {
    if (!win || !title) return;
    uint32_t i;
    for (i = 0; i < 63 && title[i]; i++) win->title[i] = title[i];
    win->title[i] = '\0';
    win->dirty = 1;
}

void window_invalidate(window_t *win) {
    if (!win) return;
    win->dirty = 1;
}

gfx_context_t *window_get_context(window_t *win) {
    if (!win) return 0;
    return win->context;
}
