#include "loopback.h"
#include "kheap.h"
#include "string.h"
#include "ethernet.h"
#include "stddef.h"

/* ------------------------------------------------------------------ */
/*  Driver state                                                       */
/* ------------------------------------------------------------------ */

static net_interface_t lo_iface;
static uint8_t         lo_mac[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ------------------------------------------------------------------ */
/*  send() callback                                                    */
/* ------------------------------------------------------------------ */
static int lo_send(net_interface_t *iface, const void *data, uint32_t len) {
    (void)iface;
    if (!data || len == 0) return -1;
    if (len > 1518) return -1;

    /* Loop back: feed the frame back into the receive path. */
    net_buffer_t *buf = net_alloc_buffer();
    if (!buf) return -1;
    memcpy(buf->data, data, len);
    buf->len    = len;
    buf->offset = 0;
    buf->iface  = iface;
    buf->next   = NULL;
    net_receive(buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  install                                                            */
/* ------------------------------------------------------------------ */
void loopback_install(void) {
    memset(&lo_iface, 0, sizeof(lo_iface));
    strncpy(lo_iface.name, "lo", sizeof(lo_iface.name) - 1);
    memcpy(lo_iface.mac.bytes, lo_mac, 6);
    lo_iface.mac = *(mac_addr_t *)lo_mac;

    lo_iface.ip.addr      = 0x0100007F; /* 127.0.0.1 in network order */
    lo_iface.mask.addr    = 0x000000FF; /* 255.0.0.0                 */
    lo_iface.gateway.addr = 0;
    lo_iface.up           = 1;
    lo_iface.mtu          = 65536;
    lo_iface.flags        = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    lo_iface.driver_data  = NULL;
    lo_iface.send         = lo_send;
    net_register_interface(&lo_iface);
}

net_interface_t *loopback_get(void) {
    return &lo_iface;
}
