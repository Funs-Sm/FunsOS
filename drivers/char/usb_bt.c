/* usb_bt.c - USB Bluetooth HCI driver.
 *
 * Manages a USB Bluetooth adapter's bulk-out, bulk-in, and interrupt-in
 * endpoints. Commands are queued via the existing USB bulk_transfer
 * primitive, and events are drained from the interrupt-in pipe by a
 * background polling task driven from the kernel main loop.
 */

#include "usb_bt.h"
#include "usb_core.h"
#include "string.h"
#include "kheap.h"

#define HCI_CMD_HEADER_SIZE 3
#define HCI_EVENT_HEADER_SIZE 2
#define HCI_ACL_HEADER_SIZE 4
#define HCI_MAX_PARAM_SIZE  255

static uint8_t  g_addr;
static uint8_t  g_ep_bulk_out;
static uint8_t  g_ep_bulk_in;
static uint8_t  g_ep_int_in;
static int      g_inited;

/* Event ring buffer for incoming HCI events. */
#define HCI_EV_RING_SIZE 32
static uint8_t   g_ev_ring[HCI_EV_RING_SIZE][512];
static uint16_t  g_ev_len[HCI_EV_RING_SIZE];
static volatile uint32_t g_ev_head;
static volatile uint32_t g_ev_tail;

/* Connection tracking. */
static usb_bt_conn_t g_conns[USB_BT_MAX_CONNECTIONS];
static int g_conn_count;

static void ev_push(const uint8_t *buf, uint16_t len) {
    uint32_t next = (g_ev_tail + 1) % HCI_EV_RING_SIZE;
    if (next == g_ev_head) return;  /* drop on overflow */
    if (len > 512) len = 512;
    memcpy(g_ev_ring[g_ev_tail], buf, len);
    g_ev_len[g_ev_tail] = len;
    g_ev_tail = next;
}

int usb_bt_init(uint8_t usb_dev_addr, uint8_t ep_bulk_out,
                uint8_t ep_bulk_in, uint8_t ep_int_in)
{
    if (g_inited) return 0;
    g_addr = usb_dev_addr;
    g_ep_bulk_out = ep_bulk_out;
    g_ep_bulk_in = ep_bulk_in;
    g_ep_int_in = ep_int_in;
    g_ev_head = g_ev_tail = 0;
    g_conn_count = 0;
    memset(g_conns, 0, sizeof(g_conns));

    /* Send RESET to initialize the controller. */
    usb_bt_send_cmd(HCI_OP_RESET, (void *)0, 0);
    g_inited = 1;
    return 0;
}

int usb_bt_send_cmd(uint16_t opcode, const void *params, uint8_t param_len) {
    if (!g_inited) return -1;
    uint32_t total = HCI_CMD_HEADER_SIZE + param_len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;
    pkt[0] = (uint8_t)(opcode);
    pkt[1] = (uint8_t)(opcode >> 8);
    pkt[2] = param_len;
    if (params && param_len) memcpy(pkt + 3, params, param_len);

    int r = usb_bulk_transfer(g_addr, g_ep_bulk_out, pkt, total);
    kfree(pkt);
    return r;
}

int usb_bt_read_event(uint8_t *buf, uint32_t buf_len) {
    if (!g_inited || g_ev_head == g_ev_tail) return 0;
    uint16_t l = g_ev_len[g_ev_head];
    if (l > buf_len) l = (uint16_t)buf_len;
    memcpy(buf, g_ev_ring[g_ev_head], l);
    g_ev_head = (g_ev_head + 1) % HCI_EV_RING_SIZE;
    return l;
}

int usb_bt_acl_tx(uint16_t handle, const void *data, uint16_t len) {
    if (!g_inited) return -1;
    uint32_t total = HCI_ACL_HEADER_SIZE + len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;
    /* handle (12 bits), packet boundary (2 bits), broadcast (2 bits) */
    uint16_t hdr = (uint16_t)(handle & 0x0FFF);
    pkt[0] = (uint8_t)(hdr);
    pkt[1] = (uint8_t)(hdr >> 8);
    pkt[2] = (uint8_t)(len);
    pkt[3] = (uint8_t)(len >> 8);
    if (data && len) memcpy(pkt + 4, data, len);

    int r = usb_bulk_transfer(g_addr, g_ep_bulk_out, pkt, total);
    kfree(pkt);
    return r;
}

int usb_bt_acl_rx(uint16_t *handle, void *buf, uint16_t buf_len) {
    if (!g_inited) return -1;
    uint8_t tmp[1024];
    int l = usb_bulk_transfer(g_addr, g_ep_bulk_in, tmp, sizeof(tmp));
    if (l <= HCI_ACL_HEADER_SIZE) return -1;
    uint16_t hdr = (uint16_t)(tmp[0] | (tmp[1] << 8));
    uint16_t len = (uint16_t)(tmp[2] | (tmp[3] << 8));
    if (handle) *handle = (uint16_t)(hdr & 0x0FFF);
    if (len > buf_len) len = buf_len;
    memcpy(buf, tmp + 4, len);
    return len;
}

int usb_bt_get_conn_count(void) { return g_conn_count; }

const usb_bt_conn_t *usb_bt_get_conn(int idx) {
    if (idx < 0 || idx >= g_conn_count) return (const usb_bt_conn_t *)0;
    return &g_conns[idx];
}