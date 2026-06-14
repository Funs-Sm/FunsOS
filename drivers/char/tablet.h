#ifndef TABLET_H
#define TABLET_H

#include "stdint.h"
#include "char.h"

/* Tablet device info */
#define TABLET_MAX_DEVICES    4
#define TABLET_MAX_PRESSURE   2048
#define TABLET_MAX_TILT       63
#define TABLET_NAME_LEN       64

/* Tablet tool types */
#define TABLET_TOOL_PEN       0x01
#define TABLET_TOOL_ERASER    0x02
#define TABLET_TOOL_PUCK      0x03
#define TABLET_TOOL_AIRBRUSH  0x04
#define TABLET_TOOL_LENS      0x05

/* Tablet button flags */
#define TABLET_BTN_STYLUS     0x01
#define TABLET_BTN_STYLUS2    0x02
#define TABLET_BTN_TIP        0x04
#define TABLET_BTN_ERASER     0x08
#define TABLET_BTN_BARREL     0x10

/* Tablet packet types */
#define TABLET_PKT_PEN        0x01
#define TABLET_PKT_PROXIMITY  0x02
#define TABLET_PKT_BUTTON     0x03
#define TABLET_PKT_STATUS     0x04

/* Tablet report descriptor types */
#define TABLET_REPORT_ID      0x01
#define TABLET_USAGE_PAGE     0x0D
#define TABLET_USAGE_DIGITIZER 0x01

/* Tablet coordinate range */
typedef struct {
    int32_t x_min;
    int32_t x_max;
    int32_t y_min;
    int32_t y_max;
    int32_t pressure_min;
    int32_t pressure_max;
    int32_t tilt_min;
    int32_t tilt_max;
    int32_t resolution_x;
    int32_t resolution_y;
} tablet_range_t;

/* Tablet pen state */
typedef struct {
    int32_t x;              /* Absolute X position */
    int32_t y;              /* Absolute Y position */
    int32_t pressure;       /* Pressure level (0 = not touching) */
    int32_t tilt_x;         /* X tilt (-63 to 63) */
    int32_t tilt_y;         /* Y tilt (-63 to 63) */
    int32_t rotation;       /* Barrel rotation */
    int32_t wheel;          /* Airbrush wheel */
    uint8_t tool_type;      /* Pen, eraser, etc. */
    uint8_t buttons;        /* Button state bitmap */
    uint8_t in_proximity;   /* Pen near tablet surface */
    uint8_t in_contact;     /* Pen touching surface */
    uint8_t tool_serial;    /* Tool serial number */
    uint32_t timestamp;     /* Last event timestamp */
} tablet_pen_state_t;

/* Tablet event */
typedef struct {
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t pressure;
    int32_t tilt_x;
    int32_t tilt_y;
    uint8_t tool_type;
    uint8_t buttons;
    uint8_t in_proximity;
    uint8_t in_contact;
    uint32_t timestamp;
} tablet_event_t;

/* Tablet device */
typedef struct {
    uint32_t id;
    char name[TABLET_NAME_LEN];
    char vendor[32];
    char product[32];
    uint32_t vendor_id;
    uint32_t product_id;
    tablet_range_t range;
    tablet_pen_state_t pen;
    tablet_pen_state_t last_pen;
    uint8_t  initialized;
    uint8_t  connected;
    uint8_t  active;
    void    *driver_data;
    /* Event callback */
    void (*event_callback)(uint32_t dev_id, tablet_event_t *event);
} tablet_dev_t;

/* Tablet API */
void tablet_init(void);
int32_t tablet_register_device(tablet_dev_t *dev);
int32_t tablet_unregister_device(uint32_t id);
tablet_dev_t *tablet_get_device(uint32_t id);
int32_t tablet_get_device_count(void);

/* Pen state queries */
int32_t tablet_get_pen_x(uint32_t dev_id);
int32_t tablet_get_pen_y(uint32_t dev_id);
int32_t tablet_get_pen_pressure(uint32_t dev_id);
int32_t tablet_get_pen_tilt_x(uint32_t dev_id);
int32_t tablet_get_pen_tilt_y(uint32_t dev_id);
uint8_t tablet_get_pen_buttons(uint32_t dev_id);
uint8_t tablet_is_pen_in_proximity(uint32_t dev_id);
uint8_t tablet_is_pen_in_contact(uint32_t dev_id);

/* Event processing */
void tablet_process_hid_report(uint32_t dev_id, const uint8_t *report, uint32_t len);
void tablet_set_event_callback(uint32_t dev_id, void (*callback)(uint32_t, tablet_event_t *));
void tablet_poll(uint32_t dev_id);

/* Coordinate conversion */
void tablet_screen_to_tablet(uint32_t dev_id, int32_t screen_x, int32_t screen_y,
                              int32_t *tablet_x, int32_t *tablet_y);
void tablet_tablet_to_screen(uint32_t dev_id, int32_t tablet_x, int32_t tablet_y,
                              int32_t *screen_x, int32_t *screen_y);

#endif