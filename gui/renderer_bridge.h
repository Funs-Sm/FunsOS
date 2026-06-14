#ifndef RENDERER_BRIDGE_H
#define RENDERER_BRIDGE_H

#include "stdint.h"
#include "gfx.h"
#include "window.h"
#include "widget.h"

/* Bridge status */
#define BRIDGE_OK        0
#define BRIDGE_NOT_INIT  -1
#define BRIDGE_NO_RENDERER -2
#define BRIDGE_ERROR     -3

/* Event conversion direction */
#define BRIDGE_EVENT_TO_SYS    0
#define BRIDGE_EVENT_FROM_SYS  1

/* Bridge state */
typedef struct {
    uint8_t  initialized;
    void    *fr_context;
    void    *fr_root_widget;
    void    *desktop_ctx;
    uint32_t render_backend;
    uint32_t screen_width;
    uint32_t screen_height;
} renderer_bridge_state_t;

/* Initialize the bridge between old gui and new renderer */
int renderer_bridge_init(uint32_t screen_w, uint32_t screen_h, uint32_t *framebuffer);

/* Shutdown the bridge */
void renderer_bridge_shutdown(void);

/* Render an old GUI widget using FunRender */
int renderer_bridge_render_gui_widget(widget_t *widget);

/* Convert old GUI events to new system events */
int renderer_bridge_convert_event(window_event_t *gui_event, void *fr_event_out,
                                   uint32_t direction);

/* Get the FunRender context for the old GUI */
void *renderer_bridge_get_context(void);

/* Sync state between old and new systems */
int renderer_bridge_sync_state(void);

/* Render a complete frame using the bridge */
int renderer_bridge_render_frame(void);

/* Switch rendering backend */
int renderer_bridge_set_backend(uint32_t backend);

/* Get current bridge state */
renderer_bridge_state_t *renderer_bridge_get_state(void);

#endif