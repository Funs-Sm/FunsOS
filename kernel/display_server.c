#include "display_server.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "keyboard.h"
#include "mouse.h"
#include "font.h"
#include "gfx.h"

#define DS_MAX_WINDOWS     32
#define DS_MAX_EVENTS      256
#define DS_TITLE_BAR_H     20
#define DS_BORDER_W        2

static uint32_t *ds_framebuffer;
static uint32_t ds_screen_w;
static uint32_t ds_screen_h;
static uint32_t ds_pitch;
static gfx_context_t ds_screen_ctx;

static ds_window_t *ds_windows[DS_MAX_WINDOWS];
static uint32_t ds_window_count;
static uint32_t ds_next_id;

static ds_event_t ds_event_queue[DS_MAX_EVENTS];
static uint32_t ds_event_head;
static uint32_t ds_event_tail;

static uint32_t ds_running;
static uint32_t ds_focused_win;

/* Mouse cursor state */
static int32_t ds_mouse_x;
static int32_t ds_mouse_y;
static uint8_t ds_mouse_buttons;

/* Drag state */
static uint32_t ds_dragging;
static uint32_t ds_drag_win;
static int32_t ds_drag_off_x;
static int32_t ds_drag_off_y;

void display_server_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch) {
    ds_framebuffer = fb;
    ds_screen_w = width;
    ds_screen_h = height;
    ds_pitch = pitch;

    gfx_init(&ds_screen_ctx, fb, width, height, pitch, 32);

    ds_window_count = 0;
    ds_next_id = 1;
    ds_event_head = 0;
    ds_event_tail = 0;
    ds_running = 0;
    ds_focused_win = 0;

    ds_mouse_x = (int32_t)(width / 2);
    ds_mouse_y = (int32_t)(height / 2);
    ds_mouse_buttons = 0;
    ds_dragging = 0;
    ds_drag_win = 0;

    memset(ds_windows, 0, sizeof(ds_windows));
    memset(ds_event_queue, 0, sizeof(ds_event_queue));
}

static void ds_enqueue_event(ds_event_t *ev) {
    uint32_t next = (ds_event_tail + 1) % DS_MAX_EVENTS;
    if (next == ds_event_head) {
        /* Drop oldest event */
        ds_event_head = (ds_event_head + 1) % DS_MAX_EVENTS;
    }
    ds_event_queue[ds_event_tail] = *ev;
    ds_event_tail = next;
}

static int ds_dequeue_event(uint32_t win_id, ds_event_t *ev) {
    uint32_t count = 0;
    uint32_t idx = ds_event_head;
    while (idx != ds_event_tail && count < DS_MAX_EVENTS) {
        if (ds_event_queue[idx].window_id == win_id || ds_event_queue[idx].window_id == 0) {
            *ev = ds_event_queue[idx];
            /* Remove by shifting */
            uint32_t cur = idx;
            uint32_t next_idx = (cur + 1) % DS_MAX_EVENTS;
            while (next_idx != ds_event_tail) {
                ds_event_queue[cur] = ds_event_queue[next_idx];
                cur = next_idx;
                next_idx = (cur + 1) % DS_MAX_EVENTS;
            }
            ds_event_tail = cur;
            return 1;
        }
        idx = (idx + 1) % DS_MAX_EVENTS;
        count++;
    }
    return 0;
}

static ds_window_t *ds_find_window(uint32_t win_id) {
    for (uint32_t i = 0; i < ds_window_count; i++) {
        if (ds_windows[i] && ds_windows[i]->window_id == win_id) {
            return ds_windows[i];
        }
    }
    return 0;
}

static void ds_bring_to_front(uint32_t win_id) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win) return;

    /* Find its index and move to end */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < ds_window_count; i++) {
        if (ds_windows[i] == win) { idx = i; break; }
    }
    for (uint32_t i = idx; i < ds_window_count - 1; i++) {
        ds_windows[i] = ds_windows[i + 1];
    }
    ds_windows[ds_window_count - 1] = win;

    /* Update focus */
    for (uint32_t i = 0; i < ds_window_count; i++) {
        ds_windows[i]->focused = 0;
    }
    win->focused = 1;
    ds_focused_win = win_id;
}

uint32_t ds_create_window(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *title) {
    if (ds_window_count >= DS_MAX_WINDOWS) return 0;

    ds_window_t *win = (ds_window_t *)kmalloc(sizeof(ds_window_t));
    if (!win) return 0;

    memset(win, 0, sizeof(ds_window_t));
    win->window_id = ds_next_id++;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->flags = 0;
    win->visible = 1;
    win->focused = 0;

    if (title) {
        strncpy(win->title, title, 127);
        win->title[127] = '\0';
    } else {
        strcpy(win->title, "Window");
    }

    /* Create offscreen buffer */
    uint32_t *buf = (uint32_t *)kmalloc(w * h * 4);
    if (!buf) {
        kfree(win);
        return 0;
    }
    memset(buf, 0, w * h * 4);

    win->ctx = (gfx_context_t *)kmalloc(sizeof(gfx_context_t));
    if (!win->ctx) {
        kfree(buf);
        kfree(win);
        return 0;
    }
    gfx_init(win->ctx, buf, w, h, w * 4, 32);

    /* Fill with white background */
    gfx_rect_t r = { 0, 0, (int32_t)w, (int32_t)h };
    gfx_fill_rect(win->ctx, r, 0xFFFFFF);

    ds_windows[ds_window_count++] = win;
    ds_bring_to_front(win->window_id);

    /* Send expose event */
    ds_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DS_EVENT_EXPOSE;
    ev.window_id = win->window_id;
    ds_enqueue_event(&ev);

    return win->window_id;
}

void ds_hide_window(uint32_t win_id) {
    ds_window_t *win = ds_find_window(win_id);
    if (win) win->visible = 0;
}

void ds_show_window(uint32_t win_id) {
    ds_window_t *win = ds_find_window(win_id);
    if (win) win->visible = 1;
}

uint32_t ds_find_window_by_title(const char *title) {
    if (!title) return 0;
    for (uint32_t i = 0; i < ds_window_count; i++) {
        ds_window_t *win = ds_windows[i];
        if (!win) continue;
        int match = 1;
        for (int j = 0; j < 127; j++) {
            if (win->title[j] != title[j]) { match = 0; break; }
            if (title[j] == '\0' && win->title[j] == '\0') break;
        }
        if (match) return win->window_id;
    }
    return 0;
}

void ds_destroy_window(uint32_t win_id) {
    for (uint32_t i = 0; i < ds_window_count; i++) {
        if (ds_windows[i] && ds_windows[i]->window_id == win_id) {
            /* Send close event */
            ds_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = DS_EVENT_WINDOW_CLOSE;
            ev.window_id = win_id;
            ds_enqueue_event(&ev);

            if (ds_windows[i]->ctx) {
                if (ds_windows[i]->ctx->buffer) {
                    kfree(ds_windows[i]->ctx->buffer);
                }
                kfree(ds_windows[i]->ctx);
            }
            kfree(ds_windows[i]);
            ds_windows[i] = 0;

            /* Compact array */
            for (uint32_t j = i; j < ds_window_count - 1; j++) {
                ds_windows[j] = ds_windows[j + 1];
            }
            ds_windows[ds_window_count - 1] = 0;
            ds_window_count--;

            if (ds_focused_win == win_id) {
                ds_focused_win = (ds_window_count > 0) ?
                    ds_windows[ds_window_count - 1]->window_id : 0;
            }
            return;
        }
    }
}

void ds_move_window(uint32_t win_id, int32_t x, int32_t y) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win) return;
    win->x = x;
    win->y = y;
}

void ds_resize_window(uint32_t win_id, uint32_t w, uint32_t h) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win) return;

    /* Allocate new buffer */
    uint32_t *new_buf = (uint32_t *)kmalloc(w * h * 4);
    if (!new_buf) return;
    memset(new_buf, 0, w * h * 4);

    /* Copy old contents */
    if (win->ctx && win->ctx->buffer) {
        uint32_t copy_w = (w < win->width) ? w : win->width;
        uint32_t copy_h = (h < win->height) ? h : win->height;
        for (uint32_t row = 0; row < copy_h; row++) {
            memcpy(new_buf + row * w,
                   win->ctx->buffer + row * win->width,
                   copy_w * 4);
        }
        kfree(win->ctx->buffer);
    }

    win->width = w;
    win->height = h;
    win->ctx->buffer = new_buf;
    win->ctx->width = w;
    win->ctx->height = h;
    win->ctx->pitch = w * 4;
    gfx_reset_clip(win->ctx);

    /* Send resize event */
    ds_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DS_EVENT_WINDOW_RESIZE;
    ev.window_id = win_id;
    ev.param1 = w;
    ev.param2 = h;
    ds_enqueue_event(&ev);
}

void ds_set_title(uint32_t win_id, const char *title) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win) return;
    if (title) {
        strncpy(win->title, title, 127);
        win->title[127] = '\0';
    }
}

void ds_invalidate(uint32_t win_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)win_id;
    (void)x; (void)y; (void)w; (void)h;
    /* Mark region as dirty - for now, just flag full repaint */
}

int ds_get_event(uint32_t win_id, ds_event_t *event) {
    return ds_dequeue_event(win_id, event);
}

void ds_draw_pixel(uint32_t win_id, uint32_t x, uint32_t y, uint32_t color) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win || !win->ctx) return;
    gfx_set_pixel(win->ctx, (int32_t)x, (int32_t)y, color);
}

void ds_draw_rect(uint32_t win_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win || !win->ctx) return;
    gfx_rect_t r = { (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h };
    gfx_fill_rect(win->ctx, r, color);
}

void ds_draw_text(uint32_t win_id, uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg) {
    ds_window_t *win = ds_find_window(win_id);
    if (!win || !win->ctx) return;
    font_draw_string(win->ctx, text, (int32_t)x, (int32_t)y, fg, bg);
}

static void ds_draw_cursor(void) {
    /* Simple arrow cursor */
    int32_t mx = ds_mouse_x;
    int32_t my = ds_mouse_y;

    for (int32_t i = 0; i < 12; i++) {
        for (int32_t j = 0; j <= i && j < 8; j++) {
            int32_t px = mx + j;
            int32_t py = my + i;
            if (px >= 0 && px < (int32_t)ds_screen_w &&
                py >= 0 && py < (int32_t)ds_screen_h) {
                ds_framebuffer[py * (ds_pitch / 4) + px] = 0x000000;
            }
        }
    }
    /* White inner part */
    for (int32_t i = 1; i < 10; i++) {
        for (int32_t j = 1; j < i && j < 6; j++) {
            int32_t px = mx + j;
            int32_t py = my + i;
            if (px >= 0 && px < (int32_t)ds_screen_w &&
                py >= 0 && py < (int32_t)ds_screen_h) {
                ds_framebuffer[py * (ds_pitch / 4) + px] = 0xFFFFFF;
            }
        }
    }
}

static void ds_draw_window_frame(ds_window_t *win) {
    int32_t x = win->x;
    int32_t y = win->y;
    uint32_t w = win->width;
    uint32_t h = win->height;

    /* Title bar background */
    uint32_t title_color = win->focused ? 0x3060C0 : 0x808080;
    for (int32_t ty = y; ty < y + DS_TITLE_BAR_H && ty < (int32_t)ds_screen_h; ty++) {
        for (int32_t tx = x; tx < x + (int32_t)w && tx < (int32_t)ds_screen_w; tx++) {
            if (tx >= 0 && ty >= 0) {
                ds_framebuffer[ty * (ds_pitch / 4) + tx] = title_color;
            }
        }
    }

    /* Title text */
    if (win->title[0]) {
        int32_t tx = x + 4;
        int32_t ty = y + 2;
        for (const char *p = win->title; *p && tx < x + (int32_t)w - 8; p++) {
            uint8_t ch = (uint8_t)*p;
            if (ch >= 32 && ch <= 127) {
                const uint8_t *glyph = font_data[ch - 32];
                for (int row = 0; row < 12; row++) {
                    uint8_t bits = glyph[row];
                    for (int col = 0; col < 8; col++) {
                        if (bits & (0x80 >> col)) {
                            int32_t px = tx + col;
                            int32_t py = ty + row;
                            if (px >= 0 && px < (int32_t)ds_screen_w &&
                                py >= 0 && py < (int32_t)ds_screen_h) {
                                ds_framebuffer[py * (ds_pitch / 4) + px] = 0xFFFFFF;
                            }
                        }
                    }
                }
            }
            tx += 8;
        }
    }

    /* Close button (small red square) */
    int32_t bx = x + (int32_t)w - 16;
    int32_t by = y + 4;
    for (int32_t dy = 0; dy < 12; dy++) {
        for (int32_t dx = 0; dx < 12; dx++) {
            int32_t px = bx + dx;
            int32_t py = by + dy;
            if (px >= 0 && px < (int32_t)ds_screen_w &&
                py >= 0 && py < (int32_t)ds_screen_h) {
                ds_framebuffer[py * (ds_pitch / 4) + px] = 0xCC0000;
            }
        }
    }

    /* Border */
    uint32_t border_color = win->focused ? 0x3060C0 : 0x606060;
    for (int32_t bx2 = x; bx2 < x + (int32_t)w && bx2 < (int32_t)ds_screen_w; bx2++) {
        if (bx2 >= 0) {
            /* Top border */
            if (y >= 0) ds_framebuffer[y * (ds_pitch / 4) + bx2] = border_color;
            /* Bottom border */
            int32_t bot = y + (int32_t)h + DS_TITLE_BAR_H;
            if (bot < (int32_t)ds_screen_h) ds_framebuffer[bot * (ds_pitch / 4) + bx2] = border_color;
        }
    }
    for (int32_t by2 = y; by2 < y + (int32_t)h + DS_TITLE_BAR_H && by2 < (int32_t)ds_screen_h; by2++) {
        if (by2 >= 0) {
            /* Left border */
            if (x >= 0) ds_framebuffer[by2 * (ds_pitch / 4) + x] = border_color;
            /* Right border */
            int32_t rx = x + (int32_t)w;
            if (rx < (int32_t)ds_screen_w) ds_framebuffer[by2 * (ds_pitch / 4) + rx] = border_color;
        }
    }
}

void ds_render_windows_only(void) {
    /* Paint windows (painter's algorithm - bottom to top).
     * Does NOT clear the screen or draw the cursor.
     * The caller is responsible for background and cursor. */
    for (uint32_t i = 0; i < ds_window_count; i++) {
        ds_window_t *win = ds_windows[i];
        if (!win || !win->visible || !win->ctx) continue;

        /* Draw window frame */
        ds_draw_window_frame(win);

        /* Blit window contents */
        int32_t dst_x = win->x;
        int32_t dst_y = win->y + DS_TITLE_BAR_H;

        for (uint32_t sy = 0; sy < win->height; sy++) {
            int32_t dy = dst_y + (int32_t)sy;
            if (dy < 0 || dy >= (int32_t)ds_screen_h) continue;
            for (uint32_t sx = 0; sx < win->width; sx++) {
                int32_t dx = dst_x + (int32_t)sx;
                if (dx < 0 || dx >= (int32_t)ds_screen_w) continue;
                uint32_t pixel = win->ctx->buffer[sy * win->width + sx];
                if (pixel != 0) {
                    ds_framebuffer[dy * (ds_pitch / 4) + dx] = pixel;
                }
            }
        }
    }
}

void ds_render(void) {
    /* Clear screen with desktop color */
    for (uint32_t y = 0; y < ds_screen_h; y++) {
        for (uint32_t x = 0; x < ds_screen_w; x++) {
            ds_framebuffer[y * (ds_pitch / 4) + x] = 0x004080;
        }
    }

    ds_render_windows_only();

    /* Draw mouse cursor */
    ds_draw_cursor();
}

void ds_process_events(void) {
    /* Process keyboard events */
    while (keyboard_has_data()) {
        keyboard_event_t ke;
        if (!keyboard_get_event(&ke)) break;

        ds_event_t ev;
        memset(&ev, 0, sizeof(ev));

        if (ke.flags & KEY_PRESSED) {
            ev.type = DS_EVENT_KEY_PRESS;
        } else {
            ev.type = DS_EVENT_KEY_RELEASE;
        }
        ev.window_id = ds_focused_win;
        ev.param1 = (uint32_t)ke.scancode;
        ev.param2 = (uint32_t)(uint8_t)ke.ascii;
        ev.param3 = (uint32_t)ke.flags;
        ds_enqueue_event(&ev);

        /* ESC to stop display server */
        if ((ke.flags & KEY_PRESSED) && ke.ascii == 27) {
            ds_running = 0;
            return;
        }
    }

    /* Process mouse events */
    while (mouse_has_data()) {
        mouse_event_t me;
        if (!mouse_get_event(&me)) break;

        ds_mouse_x += me.dx;
        ds_mouse_y -= me.dy; /* Mouse Y is inverted */
        if (ds_mouse_x < 0) ds_mouse_x = 0;
        if (ds_mouse_x >= (int32_t)ds_screen_w) ds_mouse_x = (int32_t)ds_screen_w - 1;
        if (ds_mouse_y < 0) ds_mouse_y = 0;
        if (ds_mouse_y >= (int32_t)ds_screen_h) ds_mouse_y = (int32_t)ds_screen_h - 1;

        uint8_t prev_buttons = ds_mouse_buttons;
        ds_mouse_buttons = me.buttons;

        /* Mouse move event */
        ds_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = DS_EVENT_MOUSE_MOVE;
        ev.param1 = (uint32_t)ds_mouse_x;
        ev.param2 = (uint32_t)ds_mouse_y;
        ev.param3 = (uint32_t)ds_mouse_buttons;

        /* Dragging a window */
        if (ds_dragging && (ds_mouse_buttons & MOUSE_LEFT)) {
            ds_window_t *win = ds_find_window(ds_drag_win);
            if (win) {
                win->x = ds_mouse_x - ds_drag_off_x;
                win->y = ds_mouse_y - ds_drag_off_y;
            }
        } else if (ds_dragging && !(ds_mouse_buttons & MOUSE_LEFT)) {
            ds_dragging = 0;
        }

        /* Click detection */
        if ((ds_mouse_buttons & MOUSE_LEFT) && !(prev_buttons & MOUSE_LEFT)) {
            /* Left press - check if on title bar */
            int32_t click_x = ds_mouse_x;
            int32_t click_y = ds_mouse_y;

            /* Check close button first (topmost window) */
            for (int32_t i = (int32_t)ds_window_count - 1; i >= 0; i--) {
                ds_window_t *win = ds_windows[i];
                if (!win || !win->visible) continue;

                int32_t close_x = win->x + (int32_t)win->width - 16;
                int32_t close_y = win->y + 4;
                if (click_x >= close_x && click_x < close_x + 12 &&
                    click_y >= close_y && click_y < close_y + 12) {
                    ds_destroy_window(win->window_id);
                    break;
                }

                /* Check title bar for drag */
                if (click_x >= win->x && click_x < win->x + (int32_t)win->width &&
                    click_y >= win->y && click_y < win->y + DS_TITLE_BAR_H) {
                    ds_bring_to_front(win->window_id);
                    ds_dragging = 1;
                    ds_drag_win = win->window_id;
                    ds_drag_off_x = click_x - win->x;
                    ds_drag_off_y = click_y - win->y;
                    break;
                }

                /* Check if click is inside window content area */
                if (click_x >= win->x && click_x < win->x + (int32_t)win->width &&
                    click_y >= win->y + DS_TITLE_BAR_H &&
                    click_y < win->y + DS_TITLE_BAR_H + (int32_t)win->height) {
                    ds_bring_to_front(win->window_id);
                    ev.window_id = win->window_id;

                    ds_event_t click_ev;
                    memset(&click_ev, 0, sizeof(click_ev));
                    click_ev.type = DS_EVENT_MOUSE_PRESS;
                    click_ev.window_id = win->window_id;
                    click_ev.param1 = (uint32_t)(click_x - win->x);
                    click_ev.param2 = (uint32_t)(click_y - win->y - DS_TITLE_BAR_H);
                    click_ev.param3 = MOUSE_LEFT;
                    ds_enqueue_event(&click_ev);
                    break;
                }
            }
        }

        if (!(ds_mouse_buttons & MOUSE_LEFT) && (prev_buttons & MOUSE_LEFT)) {
            ds_event_t rel_ev;
            memset(&rel_ev, 0, sizeof(rel_ev));
            rel_ev.type = DS_EVENT_MOUSE_RELEASE;
            rel_ev.param1 = (uint32_t)ds_mouse_x;
            rel_ev.param2 = (uint32_t)ds_mouse_y;
            rel_ev.param3 = MOUSE_LEFT;
            ds_enqueue_event(&rel_ev);
        }

        /* Find window under mouse for move events */
        for (int32_t i = (int32_t)ds_window_count - 1; i >= 0; i--) {
            ds_window_t *win = ds_windows[i];
            if (!win || !win->visible) continue;
            if (ds_mouse_x >= win->x && ds_mouse_x < win->x + (int32_t)win->width &&
                ds_mouse_y >= win->y + DS_TITLE_BAR_H &&
                ds_mouse_y < win->y + DS_TITLE_BAR_H + (int32_t)win->height) {
                ev.window_id = win->window_id;
                ev.param1 = (uint32_t)(ds_mouse_x - win->x);
                ev.param2 = (uint32_t)(ds_mouse_y - win->y - DS_TITLE_BAR_H);
                break;
            }
        }

        ds_enqueue_event(&ev);
    }
}

uint32_t ds_is_running(void) {
    return ds_running;
}

void ds_start(void) {
    ds_running = 1;

    /* Create a demo window */
    uint32_t win1 = ds_create_window(50, 50, 400, 300, "Display Server - Demo");
    if (win1) {
        ds_draw_text(win1, 10, 10, "Display Server Running", 0x000000, 0xFFFFFF);
        ds_draw_text(win1, 10, 30, "Press ESC to stop", 0x0000CC, 0xFFFFFF);
        ds_draw_rect(win1, 10, 60, 380, 4, 0x3060C0);
        ds_draw_text(win1, 10, 70, "Mouse: drag title bar to move", 0x333333, 0xFFFFFF);
        ds_draw_text(win1, 10, 90, "Click X button to close window", 0x333333, 0xFFFFFF);
    }

    uint32_t win2 = ds_create_window(200, 150, 300, 200, "Window 2");
    if (win2) {
        ds_draw_rect(win2, 0, 0, 300, 200, 0xE0E0FF);
        ds_draw_text(win2, 10, 10, "Another window", 0x000000, 0xE0E0FF);
    }

    /* Main loop */
    while (ds_running) {
        ds_process_events();
        ds_render();
        asm volatile("hlt");
    }

    /* Cleanup */
    for (uint32_t i = 0; i < ds_window_count; i++) {
        if (ds_windows[i]) {
            if (ds_windows[i]->ctx) {
                if (ds_windows[i]->ctx->buffer) kfree(ds_windows[i]->ctx->buffer);
                kfree(ds_windows[i]->ctx);
            }
            kfree(ds_windows[i]);
            ds_windows[i] = 0;
        }
    }
    ds_window_count = 0;

    /* Clear screen back to console */
    for (uint32_t y = 0; y < ds_screen_h; y++) {
        for (uint32_t x = 0; x < ds_screen_w; x++) {
            ds_framebuffer[y * (ds_pitch / 4) + x] = 0x000000;
        }
    }
}

void ds_stop(void) {
    ds_running = 0;
}
