#include "wm.h"
#include "font.h"
#include "gfx.h"
#include "kheap.h"
#include "string.h"
#include "rtc.h"

static window_t *top_window = 0;
static window_t *focused_window = 0;
static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static int32_t drag_start_x = 0;
static int32_t drag_start_y = 0;
static int32_t drag_win_start_x = 0;
static int32_t drag_win_start_y = 0;
static uint8_t dragging = 0;
static window_t *drag_window_ptr = 0;

/* Resize state */
static uint8_t resizing = 0;
static window_t *resize_window_ptr = 0;
static int32_t resize_start_x = 0;
static int32_t resize_start_y = 0;
static uint32_t resize_win_start_w = 0;
static uint32_t resize_win_start_h = 0;

/* Saved geometry for maximize/restore */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} saved_geo_t;

#define WM_MAX_WINDOWS 64
static saved_geo_t saved_geos[WM_MAX_WINDOWS];

static saved_geo_t *get_saved_geo(window_t *win) {
    if (!win) return 0;
    uint32_t id = win->id;
    if (id == 0 || id > WM_MAX_WINDOWS) return 0;
    return &saved_geos[id - 1];
}

void wm_init(uint32_t screen_w, uint32_t screen_h) {
    screen_width = screen_w;
    screen_height = screen_h;
    top_window = 0;
    focused_window = 0;
    dragging = 0;
    drag_window_ptr = 0;
    resizing = 0;
    resize_window_ptr = 0;
    memset(saved_geos, 0, sizeof(saved_geos));
}

void wm_add_window(window_t *win) {
    if (!win) return;
    win->next = top_window;
    if (top_window) top_window->prev = win;
    win->prev = 0;
    top_window = win;
}

void wm_remove_window(window_t *win) {
    if (!win) return;
    if (win->prev) win->prev->next = win->next;
    if (win->next) win->next->prev = win->prev;
    if (win == top_window) top_window = win->next;
    if (win == focused_window) focused_window = 0;
    if (win == drag_window_ptr) {
        drag_window_ptr = 0;
        dragging = 0;
    }
    if (win == resize_window_ptr) {
        resize_window_ptr = 0;
        resizing = 0;
    }
    win->prev = 0;
    win->next = 0;
}

static void draw_title_bar(gfx_context_t *screen, window_t *win) {
    gfx_rect_t bar = { win->x, win->y, win->width, WM_TITLE_BAR_HEIGHT };
    gfx_color_t bar_color = (win == focused_window) ? COLOR_BLUE : COLOR_GRAY;
    gfx_fill_rect(screen, bar, bar_color);

    font_draw_string(screen, win->title, win->x + 4, win->y + 2, COLOR_WHITE, 0xFFFFFFFF);

    int32_t bx = win->x + win->width - 18;
    int32_t by = win->y + 2;

    if (win->flags & WINDOW_FLAG_CLOSABLE) {
        gfx_rect_t btn = { bx, by, 14, 14 };
        gfx_fill_rect(screen, btn, COLOR_RED);
        font_draw_char(screen, 'X', bx + 3, by, COLOR_WHITE, 0xFFFFFFFF);
        bx -= 18;
    }
    if (win->flags & WINDOW_FLAG_RESIZABLE) {
        gfx_rect_t btn = { bx, by, 14, 14 };
        gfx_fill_rect(screen, btn, COLOR_GRAY);
        font_draw_char(screen, 0x7F, bx + 3, by, COLOR_WHITE, 0xFFFFFFFF);
        bx -= 18;
    }
    if (win->flags & WINDOW_FLAG_MINIMIZABLE) {
        gfx_rect_t btn = { bx, by, 14, 14 };
        gfx_fill_rect(screen, btn, COLOR_GRAY);
        font_draw_char(screen, '_', bx + 3, by, COLOR_WHITE, 0xFFFFFFFF);
    }
}

static window_t *find_bottom_window(void) {
    window_t *w = top_window;
    if (!w) return 0;
    while (w->next) w = w->next;
    return w;
}

static int32_t window_total_height(window_t *w) {
    return w->height + ((w->flags & WINDOW_FLAG_TITLE) ? WM_TITLE_BAR_HEIGHT : 0);
}

void wm_render(gfx_context_t *screen) {
    window_t *bottom = find_bottom_window();
    window_t *w = bottom;

    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE && w->state != WINDOW_STATE_MINIMIZED) {
            if (w->flags & WINDOW_FLAG_BORDER) {
                gfx_rect_t border = { w->x - WM_BORDER_WIDTH, w->y - WM_BORDER_WIDTH,
                                      w->width + WM_BORDER_WIDTH * 2, w->height + WM_BORDER_WIDTH * 2 + WM_TITLE_BAR_HEIGHT };
                gfx_draw_rect(screen, border, COLOR_DARK_GRAY);
            }

            if (w->flags & WINDOW_FLAG_TITLE) {
                draw_title_bar(screen, w);
            }

            if (w->context) {
                gfx_rect_t src = { 0, 0, w->width, w->height };
                int32_t dy = w->y + ((w->flags & WINDOW_FLAG_TITLE) ? WM_TITLE_BAR_HEIGHT : 0);
                gfx_blit(screen, w->x, dy, w->context, src);
            }

            /* Draw resize handle for resizable windows */
            if (w->flags & WINDOW_FLAG_RESIZABLE) {
                int32_t hx = w->x + w->width - WM_RESIZE_HANDLE;
                int32_t hy = w->y + window_total_height(w) - WM_RESIZE_HANDLE;
                for (int32_t i = 0; i < WM_RESIZE_HANDLE - 2; i++) {
                    gfx_draw_line(screen, hx + i, hy + WM_RESIZE_HANDLE - 2,
                                  hx + WM_RESIZE_HANDLE - 2, hy + i, COLOR_DARK_GRAY);
                }
            }
        }
        w = w->prev;
    }

    /* Draw taskbar on top of everything */
    wm_draw_taskbar(screen);
}

window_t *wm_get_window_at(int32_t x, int32_t y) {
    window_t *w = top_window;
    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE && w->state != WINDOW_STATE_MINIMIZED) {
            int32_t top = w->y;
            int32_t bottom_y = w->y + window_total_height(w);
            if (x >= w->x && x < w->x + w->width && y >= top && y < bottom_y) {
                return w;
            }
        }
        w = w->next;
    }
    return 0;
}

void wm_focus_window(window_t *win) {
    if (focused_window && focused_window != win) {
        focused_window->focused = 0;
        if (focused_window->event_handler) {
            window_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = WINDOW_EVENT_UNFOCUS;
            focused_window->event_handler(focused_window, &ev);
        }
    }
    focused_window = win;
    if (win) {
        win->focused = 1;
        if (win->event_handler) {
            window_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = WINDOW_EVENT_FOCUS;
            win->event_handler(win, &ev);
        }
    }
}

void wm_move_to_top(window_t *win) {
    if (!win || win == top_window) return;
    if (win->prev) win->prev->next = win->next;
    if (win->next) win->next->prev = win->prev;
    win->next = top_window;
    if (top_window) top_window->prev = win;
    win->prev = 0;
    top_window = win;
}

void wm_handle_mouse(int32_t x, int32_t y, uint8_t buttons) {
    /* Check if click is on taskbar */
    if (y >= (int32_t)(screen_height - WM_TASKBAR_HEIGHT)) {
        if (buttons & 0x01) {
            /* Click on taskbar - find which window button was clicked */
            int32_t bx = 4;
            window_t *w = top_window;
            /* Iterate from bottom to top for button layout */
            window_t *bottom = find_bottom_window();
            w = bottom;
            while (w) {
                uint32_t tw, th;
                font_measure_string(w->title, &tw, &th);
                int32_t btn_w = (int32_t)tw + 16;
                if (btn_w < 60) btn_w = 60;
                if (x >= bx && x < bx + btn_w) {
                    /* Clicked this window's taskbar button */
                    if (w->state == WINDOW_STATE_MINIMIZED) {
                        wm_restore_window(w);
                    }
                    wm_focus_window(w);
                    wm_move_to_top(w);
                    w->dirty = 1;
                    return;
                }
                bx += btn_w + 4;
                w = w->prev;
            }
        }
        return;
    }

    /* Handle ongoing drag */
    if (dragging) {
        if (buttons & 0x01) {
            drag_window_ptr->x = drag_win_start_x + (x - drag_start_x);
            drag_window_ptr->y = drag_win_start_y + (y - drag_start_y);
            /* Clamp to screen */
            if (drag_window_ptr->x < 0) drag_window_ptr->x = 0;
            if (drag_window_ptr->y < 0) drag_window_ptr->y = 0;
            if (drag_window_ptr->x + drag_window_ptr->width > (int32_t)screen_width)
                drag_window_ptr->x = (int32_t)screen_width - drag_window_ptr->width;
            if (drag_window_ptr->y + window_total_height(drag_window_ptr) > (int32_t)(screen_height - WM_TASKBAR_HEIGHT))
                drag_window_ptr->y = (int32_t)(screen_height - WM_TASKBAR_HEIGHT) - window_total_height(drag_window_ptr);
            drag_window_ptr->dirty = 1;
        } else {
            dragging = 0;
            drag_window_ptr = 0;
        }
        return;
    }

    /* Handle ongoing resize */
    if (resizing) {
        if (buttons & 0x01) {
            int32_t dx = x - resize_start_x;
            int32_t dy = y - resize_start_y;
            int32_t new_w = (int32_t)resize_win_start_w + dx;
            int32_t new_h = (int32_t)resize_win_start_h + dy;
            if (new_w < 80) new_w = 80;
            if (new_h < 60) new_h = 60;
            if (new_w > (int32_t)screen_width - resize_window_ptr->x)
                new_w = (int32_t)screen_width - resize_window_ptr->x;
            if (new_h > (int32_t)(screen_height - WM_TASKBAR_HEIGHT) - resize_window_ptr->y)
                new_h = (int32_t)(screen_height - WM_TASKBAR_HEIGHT) - resize_window_ptr->y;
            window_resize(resize_window_ptr, new_w, new_h);
            resize_window_ptr->dirty = 1;
        } else {
            resizing = 0;
            resize_window_ptr = 0;
        }
        return;
    }

    if (buttons & 0x01) {
        window_t *win = wm_get_window_at(x, y);
        if (win) {
            wm_focus_window(win);
            wm_move_to_top(win);

            int32_t rel_y = y - win->y;
            if (rel_y >= 0 && rel_y < WM_TITLE_BAR_HEIGHT && (win->flags & WINDOW_FLAG_TITLE)) {
                int32_t bx = win->x + win->width - 18;
                int32_t by = win->y + 2;

                if ((win->flags & WINDOW_FLAG_CLOSABLE) && x >= bx && x < bx + 14 && y >= by && y < by + 14) {
                    if (win->event_handler) {
                        window_event_t ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.type = WINDOW_EVENT_CLOSE;
                        win->event_handler(win, &ev);
                    }
                    return;
                }
                bx -= 18;
                if ((win->flags & WINDOW_FLAG_RESIZABLE) && x >= bx && x < bx + 14 && y >= by && y < by + 14) {
                    if (win->state == WINDOW_STATE_NORMAL) {
                        wm_maximize_window(win);
                    } else if (win->state == WINDOW_STATE_MAXIMIZED) {
                        wm_restore_window(win);
                    }
                    return;
                }
                bx -= 18;
                if ((win->flags & WINDOW_FLAG_MINIMIZABLE) && x >= bx && x < bx + 14 && y >= by && y < by + 14) {
                    wm_minimize_window(win);
                    return;
                }

                /* Start dragging from title bar */
                dragging = 1;
                drag_window_ptr = win;
                drag_start_x = x;
                drag_start_y = y;
                drag_win_start_x = win->x;
                drag_win_start_y = win->y;
                return;
            }

            /* Check if click is on resize handle (bottom-right corner) */
            if ((win->flags & WINDOW_FLAG_RESIZABLE) && win->state == WINDOW_STATE_NORMAL) {
                int32_t hx = win->x + win->width - WM_RESIZE_HANDLE;
                int32_t hy = win->y + window_total_height(win) - WM_RESIZE_HANDLE;
                if (x >= hx && y >= hy) {
                    resizing = 1;
                    resize_window_ptr = win;
                    resize_start_x = x;
                    resize_start_y = y;
                    resize_win_start_w = (uint32_t)win->width;
                    resize_win_start_h = (uint32_t)win->height;
                    return;
                }
            }

            /* Forward click to window */
            if (win->event_handler) {
                window_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = WINDOW_EVENT_MOUSE_PRESS;
                ev.x = x - win->x;
                ev.y = y - win->y - ((win->flags & WINDOW_FLAG_TITLE) ? WM_TITLE_BAR_HEIGHT : 0);
                ev.button = 0;
                win->event_handler(win, &ev);
            }
        }
    } else {
        window_t *win = wm_get_window_at(x, y);
        if (win && win->event_handler) {
            window_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = WINDOW_EVENT_MOUSE_MOVE;
            ev.x = x - win->x;
            ev.y = y - win->y - ((win->flags & WINDOW_FLAG_TITLE) ? WM_TITLE_BAR_HEIGHT : 0);
            win->event_handler(win, &ev);
        }
    }
}

void wm_handle_key(uint8_t scancode, uint8_t pressed) {
    if (!focused_window) return;
    if (focused_window->event_handler) {
        window_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = pressed ? WINDOW_EVENT_KEY_PRESS : WINDOW_EVENT_KEY_RELEASE;
        ev.key = scancode;
        focused_window->event_handler(focused_window, &ev);
    }
}

window_t *wm_get_focused(void) {
    return focused_window;
}

/* ---- New window management functions ---- */

void wm_move_window(window_t *win, int32_t x, int32_t y) {
    if (!win) return;
    win->x = x;
    win->y = y;
    win->dirty = 1;
}

void wm_resize_window(window_t *win, uint32_t w, uint32_t h) {
    if (!win) return;
    window_resize(win, (int32_t)w, (int32_t)h);
    win->dirty = 1;
}

void wm_minimize_window(window_t *win) {
    if (!win) return;
    if (win->state == WINDOW_STATE_MINIMIZED) return;
    saved_geo_t *sg = get_saved_geo(win);
    if (sg) {
        sg->x = win->x;
        sg->y = win->y;
        sg->width = win->width;
        sg->height = win->height;
    }
    win->state = WINDOW_STATE_MINIMIZED;
    win->flags &= ~WINDOW_FLAG_VISIBLE;
    win->dirty = 1;
    /* Focus next visible window */
    if (win == focused_window) {
        window_t *w = top_window;
        while (w) {
            if (w != win && (w->flags & WINDOW_FLAG_VISIBLE) && w->state != WINDOW_STATE_MINIMIZED) {
                wm_focus_window(w);
                break;
            }
            w = w->next;
        }
        if (w == 0) focused_window = 0;
    }
}

void wm_maximize_window(window_t *win) {
    if (!win) return;
    if (win->state == WINDOW_STATE_MAXIMIZED) return;
    saved_geo_t *sg = get_saved_geo(win);
    if (sg) {
        sg->x = win->x;
        sg->y = win->y;
        sg->width = win->width;
        sg->height = win->height;
    }
    win->x = 0;
    win->y = 0;
    window_resize(win, (int32_t)screen_width, (int32_t)(screen_height - WM_TASKBAR_HEIGHT) - WM_TITLE_BAR_HEIGHT);
    win->state = WINDOW_STATE_MAXIMIZED;
    win->dirty = 1;
}

void wm_restore_window(window_t *win) {
    if (!win) return;
    saved_geo_t *sg = get_saved_geo(win);
    if (sg && (sg->width > 0 || sg->height > 0)) {
        win->x = sg->x;
        win->y = sg->y;
        window_resize(win, sg->width, sg->height);
    }
    win->state = WINDOW_STATE_NORMAL;
    win->flags |= WINDOW_FLAG_VISIBLE;
    win->dirty = 1;
}

void wm_set_focus(window_t *win) {
    wm_focus_window(win);
    if (win) {
        wm_move_to_top(win);
        win->dirty = 1;
    }
}

void wm_draw_taskbar(gfx_context_t *ctx) {
    int32_t tb_y = (int32_t)screen_height - WM_TASKBAR_HEIGHT;

    /* Taskbar background */
    gfx_fill_rect(ctx, (gfx_rect_t){0, tb_y, (int32_t)screen_width, WM_TASKBAR_HEIGHT}, COLOR_DARK_GRAY);

    /* Top border line */
    gfx_draw_line(ctx, 0, tb_y, (int32_t)screen_width - 1, tb_y, COLOR_GRAY);

    /* Window buttons */
    int32_t bx = 4;
    window_t *bottom = find_bottom_window();
    window_t *w = bottom;
    while (w) {
        uint32_t tw, th;
        font_measure_string(w->title, &tw, &th);
        int32_t btn_w = (int32_t)tw + 16;
        if (btn_w < 60) btn_w = 60;

        gfx_color_t btn_bg = (w == focused_window) ? COLOR_BLUE : COLOR_GRAY;
        gfx_fill_rect(ctx, (gfx_rect_t){bx, tb_y + 3, btn_w, WM_TASKBAR_HEIGHT - 6}, btn_bg);
        gfx_draw_rect(ctx, (gfx_rect_t){bx, tb_y + 3, btn_w, WM_TASKBAR_HEIGHT - 6}, COLOR_LIGHT_GRAY);

        /* Show minimized indicator */
        if (w->state == WINDOW_STATE_MINIMIZED) {
            gfx_draw_rect(ctx, (gfx_rect_t){bx, tb_y + 3, btn_w, WM_TASKBAR_HEIGHT - 6}, COLOR_YELLOW);
        }

        font_draw_string(ctx, w->title, bx + 6, tb_y + 8, COLOR_WHITE, btn_bg);

        bx += btn_w + 4;
        w = w->prev;
    }

    /* Clock on the right side */
    rtc_time_t t;
    rtc_read_time(&t);
    char clock_str[16];
    /* Manual formatting since snprintf may not be available in context */
    clock_str[0] = (char)('0' + (t.hour / 10));
    clock_str[1] = (char)('0' + (t.hour % 10));
    clock_str[2] = ':';
    clock_str[3] = (char)('0' + (t.minute / 10));
    clock_str[4] = (char)('0' + (t.minute % 10));
    clock_str[5] = ':';
    clock_str[6] = (char)('0' + (t.second / 10));
    clock_str[7] = (char)('0' + (t.second % 10));
    clock_str[8] = '\0';

    uint32_t cw, ch;
    font_measure_string(clock_str, &cw, &ch);
    int32_t clock_x = (int32_t)screen_width - (int32_t)cw - 10;
    font_draw_string(ctx, clock_str, clock_x, tb_y + 8, COLOR_WHITE, COLOR_DARK_GRAY);
}

void wm_arrange_cascade(void) {
    window_t *bottom = find_bottom_window();
    window_t *w = bottom;
    int32_t offset = 0;
    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE && w->state != WINDOW_STATE_MINIMIZED) {
            if (w->state == WINDOW_STATE_MAXIMIZED) {
                wm_restore_window(w);
            }
            w->x = offset;
            w->y = offset;
            w->dirty = 1;
            offset += 24;
            if (offset + 200 > (int32_t)screen_width) offset = 0;
        }
        w = w->prev;
    }
}

void wm_arrange_tile(void) {
    /* Count visible non-minimized windows */
    int count = 0;
    window_t *w = top_window;
    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE && w->state != WINDOW_STATE_MINIMIZED) {
            count++;
        }
        w = w->next;
    }
    if (count == 0) return;

    /* Calculate tile dimensions */
    int32_t usable_w = (int32_t)screen_width;
    int32_t usable_h = (int32_t)(screen_height - WM_TASKBAR_HEIGHT);
    int32_t cols = 1;
    int32_t rows = 1;
    while (cols * rows < count) {
        if (cols <= rows) cols++;
        else rows++;
    }
    int32_t tile_w = usable_w / cols;
    int32_t tile_h = usable_h / rows;

    w = top_window;
    int idx = 0;
    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE && w->state != WINDOW_STATE_MINIMIZED) {
            if (w->state == WINDOW_STATE_MAXIMIZED) {
                wm_restore_window(w);
            }
            int32_t col = idx % cols;
            int32_t row = idx / cols;
            w->x = col * tile_w;
            w->y = row * tile_h;
            window_resize(w, tile_w - 2, tile_h - WM_TITLE_BAR_HEIGHT - 2);
            w->dirty = 1;
            idx++;
        }
        w = w->next;
    }
}

/* ---- Bridge to new desktop system ---- */

static void *desktop_context = 0;
static uint8_t desktop_registered = 0;

int wm_register_with_desktop(void) {
    if (desktop_registered) return 0;

    /* Register with the new os/desktop/ system */
    /* In production: call desktop_init(screen_width, screen_height); */
    desktop_registered = 1;
    return 0;
}

int wm_sync_to_desktop(void) {
    if (!desktop_registered) return -1;

    /* Sync old WM state to new desktop system */
    window_t *w = top_window;
    while (w) {
        if (w->flags & WINDOW_FLAG_VISIBLE) {
            /* window_mgr_create_window(w->title, w->x, w->y, w->width, w->height); */
            gui_window_register_with_sys_wm(w);
        }
        w = w->next;
    }

    return 0;
}

void wm_set_desktop_context(void *ctx) {
    desktop_context = ctx;
}

void *wm_get_desktop_context(void) {
    return desktop_context;
}
