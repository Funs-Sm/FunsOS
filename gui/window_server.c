#include "window_server.h"
#include "wm.h"
#include "compositor.h"
#include "ipc_msg.h"
#include "kheap.h"
#include "string.h"

static ws_client_t *clients = 0;
static uint32_t next_client_id = 1;

void ws_init(void) {
    clients = 0;
    next_client_id = 1;
}

void ws_run(void) {
    while (1) {
        ws_event_t input_event;
        memset(&input_event, 0, sizeof(ws_event_t));

        window_t *focused = wm_get_focused();
        if (focused) {
            ws_client_t *cl = clients;
            while (cl) {
                window_t *w = cl->windows;
                while (w) {
                    if (w->id == focused->id) {
                        ws_send_event(&input_event);
                        break;
                    }
                    w = w->next;
                }
                cl = cl->next;
            }
        }

        compositor_render();
    }
}

uint32_t ws_create_window(uint32_t client_id, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t flags) {
    ws_client_t *cl = clients;
    while (cl) {
        if (cl->client_id == client_id) break;
        cl = cl->next;
    }

    if (!cl) {
        cl = (ws_client_t *)kmalloc(sizeof(ws_client_t));
        memset(cl, 0, sizeof(ws_client_t));
        cl->client_id = client_id;
        cl->msg_queue_id = msg_create((int)client_id);
        cl->windows = 0;
        cl->next = clients;
        clients = cl;
    }

    window_t *win = window_create(NULL, "", x, y, w, h, flags);
    if (!win) return 0;

    win->next = cl->windows;
    cl->windows = win;

    return win->id;
}

int ws_destroy_window(uint32_t client_id, uint32_t win_id) {
    ws_client_t *cl = clients;
    while (cl) {
        if (cl->client_id == client_id) break;
        cl = cl->next;
    }
    if (!cl) return -1;

    window_t **pp = &cl->windows;
    while (*pp) {
        if ((*pp)->id == win_id) {
            window_t *del = *pp;
            *pp = del->next;
            window_destroy(del);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

int ws_send_event(ws_event_t *event) {
    window_t *focused = wm_get_focused();
    if (!focused) return -1;

    ws_client_t *cl = clients;
    while (cl) {
        window_t *w = cl->windows;
        while (w) {
            if (w->id == focused->id) {
                return msg_send(cl->msg_queue_id, event, sizeof(ws_event_t), 0);
            }
            w = w->next;
        }
        cl = cl->next;
    }
    return -1;
}

int ws_get_events(uint32_t client_id, ws_event_t *events, uint32_t max) {
    ws_client_t *cl = clients;
    while (cl) {
        if (cl->client_id == client_id) break;
        cl = cl->next;
    }
    if (!cl) return -1;

    uint32_t count = 0;
    while (count < max) {
        int ret = msg_recv(cl->msg_queue_id, &events[count], sizeof(ws_event_t), MSG_NOWAIT);
        if (ret != 0) break;
        count++;
    }
    return (int)count;
}
