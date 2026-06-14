#include "renderer_bridge.h"
#include "compositor.h"
#include "theme.h"
#include "wm.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static renderer_bridge_state_t bridge_state;

int renderer_bridge_init(uint32_t screen_w, uint32_t screen_h, uint32_t *framebuffer) {
    memset(&bridge_state, 0, sizeof(renderer_bridge_state_t));

    bridge_state.screen_width = screen_w;
    bridge_state.screen_height = screen_h;
    bridge_state.render_backend = 0; /* Start with software */

    /* Initialize the old compositor */
    compositor_init(framebuffer, screen_w, screen_h);

    /* Initialize the old window manager */
    wm_init(screen_w, screen_h);

    /* Set up renderer backend based on FunRender availability */
    /* In production: bridge_state.fr_context = fr_init(screen_w, screen_h, framebuffer); */
    bridge_state.fr_context = 0;

    if (bridge_state.fr_context) {
        bridge_state.render_backend = 1; /* Use FunRender */
        gfx_set_backend(GFX_BACKEND_FUNRENDER);
        gfx_set_backend_context(bridge_state.fr_context);
    }

    bridge_state.initialized = 1;
    return BRIDGE_OK;
}

void renderer_bridge_shutdown(void) {
    if (bridge_state.fr_context) {
        /* fr_shutdown(bridge_state.fr_context); */
        bridge_state.fr_context = 0;
    }

    memset(&bridge_state, 0, sizeof(renderer_bridge_state_t));
}

int renderer_bridge_render_gui_widget(widget_t *widget) {
    if (!bridge_state.initialized) return BRIDGE_NOT_INIT;
    if (!widget) return BRIDGE_ERROR;

    gfx_context_t *ctx = compositor_get_back_buffer();
    if (!ctx) return BRIDGE_NO_RENDERER;

    if (bridge_state.render_backend == 1 && bridge_state.fr_context) {
        /* Render widget using FunRender */
        widget_draw_unified(ctx, widget, 1);
    } else {
        /* Render widget using old software renderer */
        widget_draw(ctx, widget);
    }

    return BRIDGE_OK;
}

int renderer_bridge_convert_event(window_event_t *gui_event, void *fr_event_out,
                                   uint32_t direction) {
    if (!gui_event || !fr_event_out) return BRIDGE_ERROR;

    if (direction == BRIDGE_EVENT_TO_SYS) {
        /* Convert old GUI event -> new system event */
        /* Map event types and pack into new event structure */
        switch (gui_event->type) {
            case WINDOW_EVENT_MOUSE_MOVE:
            case WINDOW_EVENT_MOUSE_PRESS:
            case WINDOW_EVENT_MOUSE_RELEASE:
            case WINDOW_EVENT_KEY_PRESS:
            case WINDOW_EVENT_KEY_RELEASE:
            case WINDOW_EVENT_CLOSE:
            case WINDOW_EVENT_RESIZE:
            case WINDOW_EVENT_FOCUS:
            case WINDOW_EVENT_UNFOCUS:
                break;
            default:
                return BRIDGE_ERROR;
        }
        return BRIDGE_OK;
    } else {
        /* Convert new system event -> old GUI event */
        return BRIDGE_OK;
    }
}

void *renderer_bridge_get_context(void) {
    return bridge_state.fr_context;
}

int renderer_bridge_sync_state(void) {
    if (!bridge_state.initialized) return BRIDGE_NOT_INIT;

    /* Sync theme between systems */
    gui_theme_sync_with_fr("default");

    /* Sync compositor with FunRender */
    compositor_sync_with_funrender();

    /* Sync window manager with desktop */
    wm_sync_to_desktop();

    return BRIDGE_OK;
}

int renderer_bridge_render_frame(void) {
    if (!bridge_state.initialized) return BRIDGE_NOT_INIT;

    if (bridge_state.render_backend == 1 && bridge_state.fr_context) {
        /* Render using FunRender pipeline */
        compositor_render_to_funrender(bridge_state.fr_context);
    } else {
        /* Render using old compositor */
        compositor_render();
    }

    /* Swap buffers to display */
    compositor_swap_buffers();

    return BRIDGE_OK;
}

int renderer_bridge_set_backend(uint32_t backend) {
    if (backend > 1) return BRIDGE_ERROR;

    bridge_state.render_backend = backend;
    gfx_set_backend(backend);

    if (backend == 1) {
        gfx_set_backend_context(bridge_state.fr_context);
    } else {
        gfx_set_backend_context(0);
    }

    return BRIDGE_OK;
}

renderer_bridge_state_t *renderer_bridge_get_state(void) {
    return &bridge_state;
}