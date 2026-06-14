#ifndef DESKTOP_BRIDGE_H
#define DESKTOP_BRIDGE_H

#include "stdint.h"
#include "window.h"
#include "wm.h"

/* Desktop bridge status */
#define DBRIDGE_OK          0
#define DBRIDGE_NOT_INIT    -1
#define DBRIDGE_NO_DESKTOP  -2
#define DBRIDGE_ERROR       -3

/* Desktop bridge state */
typedef struct {
    uint8_t  initialized;
    void    *desktop_ctx;
    void    *taskbar_ctx;
    void    *start_menu_ctx;
    void    *window_mgr_ctx;
    void    *notification_ctx;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t *framebuffer;
    uint32_t pitch;
} desktop_bridge_state_t;

/* Initialize bridge to os/desktop/ */
int desktop_bridge_init(uint32_t screen_w, uint32_t screen_h, uint32_t *fb, uint32_t pitch);

/* Shutdown */
void desktop_bridge_shutdown(void);

/* Create a window in the new desktop from old GUI */
int desktop_bridge_create_window(window_t *gui_win);

/* Destroy a window */
int desktop_bridge_destroy_window(window_t *gui_win);

/* Access taskbar */
void *desktop_bridge_get_taskbar(void);

/* Access start menu */
void *desktop_bridge_get_start_menu(void);

/* Show notification via new system */
int desktop_bridge_show_notification(const char *title, const char *message, uint32_t type);

/* Sync old desktop state to new desktop */
int desktop_bridge_sync_state(void);

/* Register old windows with the new desktop */
int desktop_bridge_register_all_windows(void);

/* Get current bridge state */
desktop_bridge_state_t *desktop_bridge_get_state(void);

#endif