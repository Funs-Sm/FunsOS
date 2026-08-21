/* usb_sched.c - USB transfer scheduler.
 *
 * Schedules URBs (USB Request Blocks) into per-microframe slots with
 * bandwidth accounting. Periodic URBs (interrupt/isochronous) are
 * pinned to a specific microframe; non-periodic URBs (control/bulk)
 * go through a FIFO that the host controller driver drains.
 *
 * The microframe is a 125 us unit used on USB 2.0 High Speed and the
 * 250 us service interval on SuperSpeed. The scheduler implements the
 * "Best Fit" allocation: periodic endpoints are assigned to the
 * microframe index that minimizes aggregate residual bandwidth.
 */

#include "usb_sched.h"
#include "string.h"
#include "klog.h"

#define BANDWIDTH(b, t) ((b) > (t) ? (t) : (b))
#define MICROFRAME_BUDGET(budget) ((budget) > 8000 ? 8000 : (budget))

static uint32_t bandwidth_limit(uint8_t speed) {
    if (speed == 3) return USB_BANDWIDTH_SS_MAX;
    if (speed == 2) return USB_BANDWIDTH_HS_MAX;
    return USB_BANDWIDTH_FS_MAX;
}

int usb_sched_init(usb_sched_t *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->inited = 1;
    return 0;
}

/* Find the microframe with the most spare capacity that fits. */
static int find_best_microframe(usb_sched_t *s, uint32_t bytes) {
    int best = -1;
    uint32_t best_free = 0;
    uint32_t limit = USB_BANDWIDTH_HS_MAX;
    for (int i = 0; i < 8; i++) {
        uint32_t free = limit - s->periodic_used[i];
        if (free >= bytes && free > best_free) {
            best = i;
            best_free = free;
        }
    }
    return best;
}

int usb_sched_submit(usb_sched_t *s, usb_urb_t *urb) {
    if (!s || !urb || !s->inited) return -1;
    if (urb->transfer_size == 0) return -1;

    if (urb->ep_type == USB_EP_ISOCHRONOUS || urb->ep_type == USB_EP_INTERRUPT) {
        /* Periodic: choose a microframe. */
        int mf = find_best_microframe(s, urb->bandwidth);
        if (mf < 0) {
            s->urbs_failed++;
            return -1;
        }
        /* Insert at tail of the chosen list. */
        usb_urb_t **p = &s->periodic[mf];
        while (*p) p = &(*p)->next;
        *p = urb;
        urb->sched_class = 0;
        urb->next = (usb_urb_t *)0;
        s->periodic_count[mf]++;
        s->periodic_used[mf] += urb->bandwidth;
        urb->active = 1;
    } else {
        /* Non-periodic: append to FIFO. */
        urb->next = (usb_urb_t *)0;
        if (s->non_periodic_tail) {
            s->non_periodic_tail->next = urb;
        } else {
            s->non_periodic_head = urb;
        }
        s->non_periodic_tail = urb;
        urb->sched_class = 1;
        urb->active = 1;
        s->non_periodic_count++;
    }
    s->urbs_submitted++;
    return 0;
}

int usb_sched_cancel(usb_sched_t *s, usb_urb_t *urb) {
    if (!s || !urb) return -1;
    if (!urb->active) return 0;

    if (urb->sched_class == 0) {
        for (int mf = 0; mf < 8; mf++) {
            usb_urb_t **p = &s->periodic[mf];
            while (*p) {
                if (*p == urb) {
                    *p = urb->next;
                    urb->next = (usb_urb_t *)0;
                    urb->active = 0;
                    s->periodic_used[mf] -= urb->bandwidth;
                    s->periodic_count[mf]--;
                    return 0;
                }
                p = &(*p)->next;
            }
        }
    } else {
        usb_urb_t **p = &s->non_periodic_head;
        while (*p) {
            if (*p == urb) {
                *p = urb->next;
                if (urb == s->non_periodic_tail) {
                    s->non_periodic_tail = (usb_urb_t *)0;
                }
                urb->next = (usb_urb_t *)0;
                urb->active = 0;
                s->non_periodic_count--;
                return 0;
            }
            p = &(*p)->next;
        }
    }
    return -1;
}

int usb_sched_next_poll(usb_sched_t *s, usb_urb_t *urb, uint64_t *mf) {
    if (!s || !urb || !mf) return -1;
    if (urb->interval == 0) return -1;
    /* interval in ms; convert to microframes. */
    uint32_t microframes = (uint32_t)urb->interval * 8u;
    if (microframes == 0) microframes = 1;
    s->microframe_count++;
    *mf = (uint64_t)((s->microframe_count % 8));
    return 0;
}

int usb_sched_run_slot(usb_sched_t *s, int microframe,
                       usb_urb_t **out_list, int max_out)
{
    if (!s || microframe < 0 || microframe >= 8) return 0;
    int n = 0;
    usb_urb_t *urb = s->periodic[microframe];
    while (urb && n < max_out) {
        out_list[n++] = urb;
        urb = urb->next;
    }
    return n;
}

int usb_sched_complete(usb_sched_t *s, usb_urb_t *urb, int status) {
    if (!s || !urb) return -1;
    if (urb->complete) urb->complete(urb, status);
    /* Remove from list and decrement counters. */
    if (urb->sched_class == 0) {
        for (int mf = 0; mf < 8; mf++) {
            usb_urb_t **p = &s->periodic[mf];
            while (*p) {
                if (*p == urb) {
                    *p = urb->next;
                    urb->active = 0;
                    s->periodic_count[mf]--;
                    if (mf == (int)(s->microframe_count % 8)) {
                        /* Free the bandwidth if it's the same microframe. */
                        s->periodic_used[mf] -= urb->bandwidth;
                    } else {
                        /* Approximate - clear all to avoid drift. */
                        s->periodic_used[mf] = 0;
                    }
                    s->urbs_completed++;
                    return 0;
                }
                p = &(*p)->next;
            }
        }
    } else {
        usb_urb_t **p = &s->non_periodic_head;
        while (*p) {
            if (*p == urb) {
                *p = urb->next;
                if (urb == s->non_periodic_tail) {
                    s->non_periodic_tail = (usb_urb_t *)0;
                }
                urb->active = 0;
                s->non_periodic_count--;
                s->urbs_completed++;
                return 0;
            }
            p = &(*p)->next;
        }
    }
    return -1;
}

void usb_sched_print(usb_sched_t *s) {
    if (!s) return;
    klog_write(KLOG_INFO,
               "usb-sched: uframe=%llu sub=%llu comp=%llu fail=%llu np=%u\n",
               s->microframe_count, s->urbs_submitted,
               s->urbs_completed, s->urbs_failed,
               s->non_periodic_count);
    for (int i = 0; i < 8; i++) {
        if (s->periodic_count[i] > 0) {
            klog_write(KLOG_INFO,
                       "  uframe[%d]: count=%d used=%u/%u\n",
                       i, s->periodic_count[i], s->periodic_used[i],
                       USB_BANDWIDTH_HS_MAX);
        }
    }
}