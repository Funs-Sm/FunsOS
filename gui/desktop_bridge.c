#include "desktop_bridge.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static desktop_bridge_state_t dbridge_state;

int desktop_bridge_init(uint32_t screen_w, uint32_t screen_h, uint32_t *fb, uint32_t pitch) {
    memset(&dbridge_state, 0, sizeof(desktop_bridge_state_t));

    dbridge_state.screen_width = screen_w;
    dbridge_state.screen_height = screen_h;
    dbridge_state.framebuffer = fb;
    dbridge_state.pitch = pitch;

    /* Initialize the new desktop system */
    /* In production:
        gui_core_init(screen_w, screen_h, fb, pitch);
        window_mgr_init(screen_w, screen_h);
        taskbar_init(screen_w, screen_h, fb, pitch);
        start_menu_init(screen_w, screen_h, fb, pitch);
        notification_init();
    */

    dbridge_state.desktop_ctx = (void *)1;  /* Placeholder */
    dbridge_state.taskbar_ctx = (void *)1;
    dbridge_state.start_menu_ctx = (void *)1;
    dbridge_state.window_mgr_ctx = (void *)1;
    dbridge_state.notification_ctx = (void *)1;

    /* Initialize the old window manager and register with desktop */
    wm_init(screen_w, screen_h);
    wm_register_with_desktop();

    dbridge_state.initialized = 1;

    return DBRIDGE_OK;
}

void desktop_bridge_shutdown(void) {
    /* In production:
        notification_clear_all();
        start_menu_cleanup();
        taskbar_cleanup();
        window_mgr_cleanup();
        gui_core_shutdown();
        desktop_shutdown();
    */

    memset(&dbridge_state, 0, sizeof(desktop_bridge_state_t));
}

int desktop_bridge_create_window(window_t *gui_win) {
    if (!dbridge_state.initialized) return DBRIDGE_NOT_INIT;
    if (!gui_win) return DBRIDGE_ERROR;

    /* Register the old GUI window with the new window manager */
    gui_window_register_with_sys_wm(gui_win);

    /* In production:
        uint32_t win_id = window_mgr_create_window(
            gui_win->title, gui_win->x, gui_win->y,
            gui_win->width, gui_win->height);

        taskbar_add_window_button(win_id, gui_win->title);
    */

    return DBRIDGE_OK;
}

int desktop_bridge_destroy_window(window_t *gui_win) {
    if (!dbridge_state.initialized) return DBRIDGE_NOT_INIT;
    if (!gui_win) return DBRIDGE_ERROR;

    /* In production:
        sys_window_handle_t h = gui_window_to_sys_window(gui_win);
        if (h) {
            taskbar_remove_window_button(gui_win->id);
            window_mgr_close_window(h);
        }
    */

    return DBRIDGE_OK;
}

void *desktop_bridge_get_taskbar(void) {
    return dbridge_state.taskbar_ctx;
}

void *desktop_bridge_get_start_menu(void) {
    return dbridge_state.start_menu_ctx;
}

int desktop_bridge_show_notification(const char *title, const char *message, uint32_t type) {
    if (!dbridge_state.initialized) return DBRIDGE_NOT_INIT;

    /* In production:
        notification_send(title, message, type);
    */

    return DBRIDGE_OK;
}

int desktop_bridge_sync_state(void) {
    if (!dbridge_state.initialized) return DBRIDGE_NOT_INIT;

    /* Sync old desktop state to new desktop */
    wm_sync_to_desktop();

    /* In production:
        desktop_refresh();
    */

    return DBRIDGE_OK;
}

int desktop_bridge_register_all_windows(void) {
    if (!dbridge_state.initialized) return DBRIDGE_NOT_INIT;

    /* Iterate through all old-style windows and register them */
    return wm_sync_to_desktop();
}

desktop_bridge_state_t *desktop_bridge_get_state(void) {
    return &dbridge_state;
}