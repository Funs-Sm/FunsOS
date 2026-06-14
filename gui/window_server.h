#ifndef WINDOW_SERVER_H
#define WINDOW_SERVER_H

#include "window.h"
#include "stdint.h"

#define WS_EVENT_MOUSE   1
#define WS_EVENT_KEY     2
#define WS_EVENT_WINDOW  3

typedef struct {
    uint32_t type;
    int32_t x;
    int32_t y;
    uint8_t button;
    uint8_t key;
    uint32_t window_id;
    uint32_t event_type;
} ws_event_t;

typedef struct ws_client {
    uint32_t client_id;
    int32_t msg_queue_id;
    window_t *windows;
    struct ws_client *next;
} ws_client_t;

void ws_init(void);
void ws_run(void);
uint32_t ws_create_window(uint32_t client_id, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t flags);
int ws_destroy_window(uint32_t client_id, uint32_t win_id);
int ws_send_event(ws_event_t *event);
int ws_get_events(uint32_t client_id, ws_event_t *events, uint32_t max);

#endif
