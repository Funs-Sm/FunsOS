#include "gui_apps.h"
#include "window.h"
#include "widget.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "kheap.h"
#include "string.h"
#include "version.h"
#include "smp.h"
#include "pmm.h"
#include "net.h"
#include "dhcp.h"
#include "arp.h"
#include "stdio.h"
#include "vfs.h"
#include "path.h"
#include "dentry.h"
#include "rtc.h"

/* ------------------------------------------------------------------ */
/*  Settings Application                                               */
/* ------------------------------------------------------------------ */

static window_t *settings_win;
static widget_t *settings_btn_light;
static widget_t *settings_btn_dark;

static void settings_event_handler(window_t *win, window_event_t *event) {
    if (!win || !event) return;

    if (event->type == WINDOW_EVENT_MOUSE_PRESS) {
        /* Check if click is on Light button */
        if (settings_btn_light) {
            if (event->x >= settings_btn_light->bounds.x &&
                event->x < settings_btn_light->bounds.x + settings_btn_light->bounds.w &&
                event->y >= settings_btn_light->bounds.y &&
                event->y < settings_btn_light->bounds.y + settings_btn_light->bounds.h) {
                theme_set("Light");
                /* Redraw window with new theme */
                theme_apply_window(win);
                gfx_context_t *ctx = window_get_context(win);
                if (ctx) {
                    theme_t t = theme_get();
                    gfx_fill_rect(ctx, (gfx_rect_t){0, 0, (int32_t)ctx->width, (int32_t)ctx->height}, t.window_bg);
                    font_draw_string(ctx, "Settings", 10, 8, t.title_text_color, t.title_bar_color);
                    int32_t y = 50;
                    font_draw_string(ctx, "Theme", 20, y, t.fg_color, t.window_bg);
                    y += 30;
                    widget_draw(ctx, settings_btn_light);
                    widget_draw(ctx, settings_btn_dark);
                    y += 50;
                    font_draw_string(ctx, "Display", 20, y, t.fg_color, t.window_bg);
                    y += 24;
                    char res_str[64];
                    sprintf(res_str, "Resolution: %u x %u", ctx->width, ctx->height);
                    font_draw_string(ctx, res_str, 30, y, t.fg_color, t.window_bg);
                    y += 40;
                    font_draw_string(ctx, "About", 20, y, t.fg_color, t.window_bg);
                    y += 24;
                    font_draw_string(ctx, KERNEL_STRING, 30, y, t.fg_color, t.window_bg);
                    /* Theme indicator */
                    y += 30;
                    font_draw_string(ctx, "Current: Light", 30, y, COLOR_GREEN, t.window_bg);
                }
                win->dirty = 1;
                return;
            }
        }
        /* Check if click is on Dark button */
        if (settings_btn_dark) {
            if (event->x >= settings_btn_dark->bounds.x &&
                event->x < settings_btn_dark->bounds.x + settings_btn_dark->bounds.w &&
                event->y >= settings_btn_dark->bounds.y &&
                event->y < settings_btn_dark->bounds.y + settings_btn_dark->bounds.h) {
                theme_set("Dark");
                /* Redraw window with new theme */
                theme_apply_window(win);
                gfx_context_t *ctx = window_get_context(win);
                if (ctx) {
                    theme_t t = theme_get();
                    gfx_fill_rect(ctx, (gfx_rect_t){0, 0, (int32_t)ctx->width, (int32_t)ctx->height}, t.window_bg);
                    font_draw_string(ctx, "Settings", 10, 8, t.title_text_color, t.title_bar_color);
                    int32_t y = 50;
                    font_draw_string(ctx, "Theme", 20, y, t.fg_color, t.window_bg);
                    y += 30;
                    widget_draw(ctx, settings_btn_light);
                    widget_draw(ctx, settings_btn_dark);
                    y += 50;
                    font_draw_string(ctx, "Display", 20, y, t.fg_color, t.window_bg);
                    y += 24;
                    char res_str[64];
                    sprintf(res_str, "Resolution: %u x %u", ctx->width, ctx->height);
                    font_draw_string(ctx, res_str, 30, y, t.fg_color, t.window_bg);
                    y += 40;
                    font_draw_string(ctx, "About", 20, y, t.fg_color, t.window_bg);
                    y += 24;
                    font_draw_string(ctx, KERNEL_STRING, 30, y, t.fg_color, t.window_bg);
                    /* Theme indicator */
                    y += 30;
                    font_draw_string(ctx, "Current: Dark", 30, y, COLOR_CYAN, t.window_bg);
                }
                win->dirty = 1;
                return;
            }
        }
    }
}

void gui_app_settings(void) {
    settings_win = window_create(0, "Settings", 100, 80, 400, 320,
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLE |
        WINDOW_FLAG_CLOSABLE);
    if (!settings_win) return;

    theme_apply_window(settings_win);
    gfx_context_t *ctx = window_get_context(settings_win);
    if (!ctx) return;

    /* Background */
    theme_t t = theme_get();
    gfx_fill_rect(ctx, (gfx_rect_t){0, 0, 400, 320}, t.window_bg);

    /* Title bar area */
    font_draw_string(ctx, "Settings", 10, 8, t.title_text_color, t.title_bar_color);

    /* Theme section */
    int32_t y = 50;
    font_draw_string(ctx, "Theme", 20, y, t.fg_color, t.window_bg);
    y += 30;

    settings_btn_light = widget_create_button(20, y, 100, 28, "Light");
    widget_draw(ctx, settings_btn_light);

    settings_btn_dark = widget_create_button(140, y, 100, 28, "Dark");
    widget_draw(ctx, settings_btn_dark);

    y += 50;
    /* Display resolution section */
    font_draw_string(ctx, "Display", 20, y, t.fg_color, t.window_bg);
    y += 24;
    char res_str[64];
    sprintf(res_str, "Resolution: %u x %u", ctx->width, ctx->height);
    font_draw_string(ctx, res_str, 30, y, t.fg_color, t.window_bg);

    y += 40;
    /* About section */
    font_draw_string(ctx, "About", 20, y, t.fg_color, t.window_bg);
    y += 24;
    font_draw_string(ctx, KERNEL_STRING, 30, y, t.fg_color, t.window_bg);

    /* Set event handler for theme switching */
    settings_win->event_handler = settings_event_handler;

    window_show(settings_win);
}

/* ------------------------------------------------------------------ */
/*  File Manager Application                                           */
/* ------------------------------------------------------------------ */

static window_t *fm_win;
static char fm_current_dir[256] = "/";

static void fm_draw_contents(gfx_context_t *ctx, window_t *win) {
    if (!ctx) return;
    theme_t t = theme_get();

    /* Clear window content area */
    gfx_fill_rect(ctx, (gfx_rect_t){0, 0, (int32_t)ctx->width, (int32_t)ctx->height}, t.window_bg);

    /* Title bar */
    font_draw_string(ctx, "File Manager", 10, 8, t.title_text_color, t.title_bar_color);

    /* Current directory path */
    int32_t y = 44;
    gfx_fill_rect(ctx, (gfx_rect_t){10, y, (int32_t)ctx->width - 20, 24}, t.text_bg);
    gfx_draw_rect(ctx, (gfx_rect_t){10, y, (int32_t)ctx->width - 20, 24}, COLOR_GRAY);
    font_draw_string(ctx, fm_current_dir, 16, y + 4, t.text_fg, t.text_bg);

    /* File listing area */
    y += 32;
    int32_t list_h = (int32_t)ctx->height - y - 24;
    gfx_fill_rect(ctx, (gfx_rect_t){10, y, (int32_t)ctx->width - 20, list_h}, t.text_bg);
    gfx_draw_rect(ctx, (gfx_rect_t){10, y, (int32_t)ctx->width - 20, list_h}, COLOR_GRAY);

    /* List directory contents using VFS */
    dentry_t *dir = 0;
    int item_count = 0;

    if (path_resolve(fm_current_dir, &dir) == 0 && dir && dir->inode && (dir->inode->mode & FILE_MODE_DIR)) {
        dentry_t *child = dir->child;
        int32_t fy = y + 4;
        while (child && fy + 18 < y + list_h - 4) {
            char entry_line[280];
            uint32_t i;
            for (i = 0; i < 254 && child->name[i]; i++) entry_line[i] = child->name[i];

            if (child->inode && (child->inode->mode & FILE_MODE_DIR)) {
                entry_line[i] = '/';
                entry_line[i + 1] = '\0';
            } else {
                entry_line[i] = '\0';
                /* Show file size if available */
                if (child->inode && child->inode->size > 0) {
                    char size_str[32];
                    sprintf(size_str, " (%u B)", child->inode->size);
                    uint32_t slen = 0;
                    while (size_str[slen]) slen++;
                    for (uint32_t j = 0; j < slen && i + j < 278; j++) {
                        entry_line[i + j] = size_str[j];
                    }
                    entry_line[i + slen] = '\0';
                }
            }

            font_draw_string(ctx, entry_line, 18, fy, t.fg_color, t.text_bg);
            fy += 20;
            child = child->next_sibling;
            item_count++;
        }
    }

    /* Status bar */
    char status[64];
    sprintf(status, "%d items", item_count);
    font_draw_string(ctx, status, 10, (int32_t)ctx->height - 16, t.fg_color, t.window_bg);

    win->dirty = 1;
}

void gui_app_filemanager(void) {
    fm_win = window_create(0, "File Manager", 120, 60, 500, 380,
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLE |
        WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_RESIZABLE);
    if (!fm_win) return;

    theme_apply_window(fm_win);
    gfx_context_t *ctx = window_get_context(fm_win);
    if (!ctx) return;

    /* Start at root */
    strcpy(fm_current_dir, "/");

    fm_draw_contents(ctx, fm_win);

    window_show(fm_win);
}

/* ------------------------------------------------------------------ */
/*  System Info Application                                            */
/* ------------------------------------------------------------------ */

void gui_app_sysinfo(void) {
    window_t *si_win = window_create(0, "System Info", 80, 50, 460, 400,
        WINDOW_FLAG_VISIBLE | WINDOW_FLAG_BORDER | WINDOW_FLAG_TITLE |
        WINDOW_FLAG_CLOSABLE);
    if (!si_win) return;

    theme_apply_window(si_win);
    gfx_context_t *ctx = window_get_context(si_win);
    if (!ctx) return;

    theme_t t = theme_get();
    gfx_fill_rect(ctx, (gfx_rect_t){0, 0, 460, 400}, t.window_bg);

    /* Title bar */
    font_draw_string(ctx, "System Info", 10, 8, t.title_text_color, t.title_bar_color);

    int32_t y = 44;
    char line[128];

    /* CPU Info section */
    font_draw_string(ctx, "CPU", 20, y, t.accent_color, t.window_bg);
    y += 22;
    sprintf(line, "  Cores: %u", smp_get_cpu_count());
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
    y += 18;
    font_draw_string(ctx, "  Vendor: GenuineIntel", 20, y, t.fg_color, t.window_bg);
    y += 18;
    font_draw_string(ctx, "  Architecture: x86 (32-bit)", 20, y, t.fg_color, t.window_bg);

    y += 30;
    /* Memory Info section - real data from PMM */
    font_draw_string(ctx, "Memory", 20, y, t.accent_color, t.window_bg);
    y += 22;
    uint32_t total_kb = (pmm_get_total_pages() * PMM_PAGE_SIZE) / 1024;
    uint32_t used_kb  = (pmm_get_used_pages() * PMM_PAGE_SIZE) / 1024;
    uint32_t free_kb  = (pmm_get_free_pages() * PMM_PAGE_SIZE) / 1024;
    sprintf(line, "  Total: %u kB", total_kb);
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
    y += 18;
    sprintf(line, "  Used:  %u kB", used_kb);
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
    y += 18;
    sprintf(line, "  Free:  %u kB", free_kb);
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);

    /* Memory usage bar */
    y += 22;
    int32_t bar_x = 30;
    int32_t bar_w = 400;
    int32_t bar_h = 12;
    gfx_fill_rect(ctx, (gfx_rect_t){bar_x, y, bar_w, bar_h}, COLOR_DARK_GRAY);
    if (total_kb > 0) {
        int32_t used_w = (int32_t)((uint32_t)used_kb * (uint32_t)bar_w / (uint32_t)total_kb);
        if (used_w > bar_w) used_w = bar_w;
        gfx_fill_rect(ctx, (gfx_rect_t){bar_x, y, used_w, bar_h}, COLOR_BLUE);
    }
    y += 20;

    /* Disk Info section - try to get real data from VFS */
    font_draw_string(ctx, "Disk", 20, y, t.accent_color, t.window_bg);
    y += 22;

    /* Check root filesystem info */
    dentry_t *root = 0;
    if (path_resolve("/", &root) == 0 && root && root->inode && root->inode->sb) {
        superblock_t *sb = root->inode->sb;
        sprintf(line, "  Type: %s", sb->fs_type == FS_TYPE_FAT32 ? "FAT32" :
                         sb->fs_type == FS_TYPE_EXT2 ? "ext2" :
                         sb->fs_type == FS_TYPE_RAMFS ? "ramfs" :
                         sb->fs_type == FS_TYPE_DEVFS ? "devfs" : "unknown");
        font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
        y += 18;

        if (sb->total_blocks > 0) {
            uint32_t total_disk = sb->total_blocks * sb->block_size;
            uint32_t free_disk = sb->free_blocks * sb->block_size;
            sprintf(line, "  Size: %u KB (%u blocks)", total_disk / 1024, sb->total_blocks);
            font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
            y += 18;
            sprintf(line, "  Free: %u KB (%u blocks)", free_disk / 1024, sb->free_blocks);
            font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
        } else {
            font_draw_string(ctx, "  (no disk info available)", 20, y, t.fg_color, t.window_bg);
        }
    } else {
        font_draw_string(ctx, "  (no filesystem mounted)", 20, y, t.fg_color, t.window_bg);
    }

    y += 30;
    /* Network Info section - real data from net stack */
    font_draw_string(ctx, "Network", 20, y, t.accent_color, t.window_bg);
    y += 22;
    uint32_t if_count = net_get_interface_count();
    sprintf(line, "  Interfaces: %u", if_count);
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
    y += 18;

    net_interface_t *def_iface = net_get_default_interface();
    if (def_iface) {
        sprintf(line, "  Default: %s", def_iface->name);
        font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
        y += 18;
        sprintf(line, "  IP: %u.%u.%u.%u",
            (def_iface->ip.addr >> 24) & 0xFF,
            (def_iface->ip.addr >> 16) & 0xFF,
            (def_iface->ip.addr >>  8) & 0xFF,
            (def_iface->ip.addr      ) & 0xFF);
        font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
        y += 18;
        sprintf(line, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
            def_iface->mac.bytes[0], def_iface->mac.bytes[1], def_iface->mac.bytes[2],
            def_iface->mac.bytes[3], def_iface->mac.bytes[4], def_iface->mac.bytes[5]);
        font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);
    } else {
        font_draw_string(ctx, "  No interface", 20, y, t.fg_color, t.window_bg);
    }

    /* RTC time */
    y += 30;
    font_draw_string(ctx, "Time", 20, y, t.accent_color, t.window_bg);
    y += 22;
    rtc_time_t rtc;
    rtc_read_time(&rtc);
    sprintf(line, "  %04u-%02u-%02u %02u:%02u:%02u",
            rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
    font_draw_string(ctx, line, 20, y, t.fg_color, t.window_bg);

    /* Kernel version at bottom */
    font_draw_string(ctx, KERNEL_STRING, 20, 374, COLOR_GRAY, t.window_bg);

    window_show(si_win);
}
