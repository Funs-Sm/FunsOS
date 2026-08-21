#ifndef USB_SCHED_H
#define USB_SCHED_H

#include "stdint.h"

/* USB Transfer Scheduler.
 *
 * Implements:
 *   - Bandwidth allocation per USB 2.0 / USB 3.0 frame (microframe for HS/SS)
 *   - Periodic vs non-periodic endpoint reservation
 *   - Isochronous, interrupt, control, bulk endpoint scheduling
 *
 * The host controller driver hands URBs over to the scheduler; the
 * scheduler decides which URBs are eligible for the next scheduling
 * slot and hands them back to the HC for execution.
 *
 * The model used here is the Linux-style "uframe_scheduler" but
 * simplified to a single periodic array and one non-periodic queue.
 */

#define USB_BANDWIDTH_FS_MAX        1200   /* bytes per microframe in FS mode */
#define USB_BANDWIDTH_HS_MAX        8000   /* 4 microframes = 1 frame at HS */
#define USB_BANDWIDTH_SS_MAX       500000  /* bytes per microframe at SS */

/* Endpoint types */
#define USB_EP_CONTROL     0
#define USB_EP_ISOCHRONOUS 1
#define USB_EP_BULK        2
#define USB_EP_INTERRUPT   3

#define USB_MAX_URBS           512
#define USB_MAX_EP_PER_DEVICE  32
#define USB_MAX_SCHED_BUDGET   8

typedef struct usb_urb {
    uint8_t  ep_type;
    uint8_t  speed;            /* 0=LS, 1=FS, 2=HS, 3=SS */
    uint8_t  ep_num;
    uint8_t  direction;        /* 0=OUT, 1=IN */
    uint32_t transfer_size;
    uint32_t bandwidth;        /* bytes per microframe */
    uint32_t interval;         /* polling interval (ms/us units) */
    uint32_t budget_us;        /* microseconds consumed per service */
    void    *data;
    uint32_t data_len;
    int      active;
    /* Linkage. */
    struct usb_urb *next;
    /* Completion callback. */
    void (*complete)(struct usb_urb *urb, int status);
    void *context;
    /* Time of last scheduling (in microframes). */
    uint64_t last_scheduled;
    /* Scheduling class. */
    uint8_t  sched_class;      /* 0=periodic, 1=non-periodic */
} usb_urb_t;

/* Endpoint reservation slot. */
typedef struct {
    uint8_t  ep_num;
    uint8_t  type;
    uint8_t  speed;
    uint16_t max_packet;
    uint32_t bandwidth;
    uint32_t interval;
    uint32_t budget_us;
    uint8_t  reserved;
} usb_ep_reservation_t;

typedef struct {
    /* Periodic scheduling state (per-microframe array). */
    usb_urb_t *periodic[8];   /* up to 8 microframes per frame */
    int        periodic_count[8];
    uint32_t   periodic_used[8];   /* bytes used per microframe */

    /* Non-periodic queue. */
    usb_urb_t *non_periodic_head;
    usb_urb_t *non_periodic_tail;
    uint32_t   non_periodic_count;

    /* Global state. */
    uint64_t   microframe_count;
    int        inited;
    /* Stats. */
    uint64_t   urbs_submitted;
    uint64_t   urbs_completed;
    uint64_t   urbs_failed;
} usb_sched_t;

int  usb_sched_init(usb_sched_t *s);

/* Submit a URB to the scheduler. */
int  usb_sched_submit(usb_sched_t *s, usb_urb_t *urb);

/* Cancel a previously submitted URB. */
int  usb_sched_cancel(usb_sched_t *s, usb_urb_t *urb);

/* Compute next microframe when a periodic URB is due.
 * Returns the microframe index (0..7) and updates *mf. */
int  usb_sched_next_poll(usb_sched_t *s, usb_urb_t *urb, uint64_t *microframe);

/* Service one microframe: returns up to MAX_REQS URBs ready to execute. */
int  usb_sched_run_slot(usb_sched_t *s, int microframe,
                         usb_urb_t **out_list, int max_out);

/* Mark URB complete, freeing scheduling slots. */
int  usb_sched_complete(usb_sched_t *s, usb_urb_t *urb, int status);

/* Stats / diagnostics. */
void usb_sched_print(usb_sched_t *s);

#endif