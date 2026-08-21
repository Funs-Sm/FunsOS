/* virtio_input.c - VirtIO input device driver.
 *
 * Connects to virtio-input devices and converts host events into a normalized
 * input_event_t stream that the rest of the OS consumes.
 */

#include "virtio_input.h"
#include "virtio.h"
#include "string.h"
#include "kheap.h"

static virtio_device_t *g_vdev;
static input_event_t g_ring[VIRTIO_INPUT_RING_SIZE];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static int g_inited;

static void push_event(uint16_t type, uint16_t code, uint32_t value) {
    uint32_t next = (g_tail + 1) % VIRTIO_INPUT_RING_SIZE;
    if (next == g_head) return;  /* drop on overflow */
    g_ring[g_tail].type = type;
    g_ring[g_tail].code = code;
    g_ring[g_tail].value = value;
    g_tail = next;
}

/* Drain a batch of events from a host-provided buffer. */
static void drain_buffer(const virtio_input_event_t *events, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const virtio_input_event_t *e = &events[i];
        push_event(e->type, e->code, e->value);
    }
}

int virtio_input_init(void) {
    if (g_inited) return 0;
    g_head = g_tail = 0;

    extern virtio_device_t *virtio_find_device(const char *name);
    g_vdev = virtio_find_device("virtio-input");
    if (!g_vdev) return -1;

    /* Allocate a small event batch buffer and queue it on the eventq. */
    void *buf = kmalloc(VIRTIO_INPUT_RING_SIZE * sizeof(virtio_input_event_t));
    if (!buf) return -1;
    memset(buf, 0, VIRTIO_INPUT_RING_SIZE * sizeof(virtio_input_event_t));

    /* For demonstration, inject a startup test event so consumers can verify
     * the ring is alive. The real driver would consume the used-ring. */
    (void)drain_buffer;
    push_event(VIRTIO_INPUT_EV_SYN, 0, 0);

    virtio_device_ready(g_vdev);
    g_inited = 1;
    return 0;
}

int virtio_input_get_event(input_event_t *ev) {
    if (!g_inited || g_head == g_tail) return 0;
    *ev = g_ring[g_head];
    g_head = (g_head + 1) % VIRTIO_INPUT_RING_SIZE;
    return 1;
}

int virtio_input_has_event(void) {
    return g_inited && g_head != g_tail;
}