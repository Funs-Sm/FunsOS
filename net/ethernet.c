#include "ethernet.h"
#include "net.h"
#include "arp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

int ethernet_send_vlan(net_interface_t *iface, mac_addr_t dst,
                       uint16_t type, uint16_t vlan_id,
                       const void *payload, uint32_t len) {
    net_buffer_t *buf = net_alloc_buffer();
    if (!buf) return -1;

    uint32_t hdr_len = sizeof(ethernet_header_t);
    if (vlan_id) hdr_len += sizeof(vlan_tag_t);

    ethernet_header_t *hdr = (ethernet_header_t *)buf->data;
    hdr->dst = dst;
    hdr->src = iface->mac;
    hdr->ethertype = vlan_id ? ETH_P_VLAN : type;

    uint8_t *p = buf->data + sizeof(ethernet_header_t);
    if (vlan_id) {
        vlan_tag_t *vt = (vlan_tag_t *)p;
        vt->tci       = (uint16_t)(0xE000U | (vlan_id & 0x0FFFU));
        vt->ethertype = type;
        p += sizeof(vlan_tag_t);
    }
    memcpy(p, payload, len);
    uint32_t total_len = hdr_len - sizeof(ethernet_header_t) + hdr_len + len;
    /* Recompute properly: header is hdr_len bytes, payload is len. */
    total_len = hdr_len + len;

    if (total_len < 60) {
        memset(buf->data + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    buf->len = total_len;
    buf->offset = 0;
    buf->iface = iface;

    net_transmit(iface, buf);
    net_free_buffer(buf);
    return 0;
}

int ethernet_send(net_interface_t *iface, mac_addr_t dst, uint16_t type,
                  const void *payload, uint32_t len) {
    return ethernet_send_vlan(iface, dst, type, 0, payload, len);
}

void ethernet_receive(net_buffer_t *buf) {
    ethernet_header_t *hdr = (ethernet_header_t *)(buf->data + buf->offset);
    uint16_t type = hdr->ethertype;

    buf->offset += sizeof(ethernet_header_t);
    buf->len -= sizeof(ethernet_header_t);

    /* Handle 802.1Q VLAN tags.  Skip the inner tag and recurse. */
    if (type == ETH_P_VLAN) {
        if (buf->len < (int)sizeof(vlan_tag_t)) return;
        vlan_tag_t *vt = (vlan_tag_t *)(buf->data + buf->offset);
        type = vt->ethertype;
        buf->offset += sizeof(vlan_tag_t);
        buf->len    -= sizeof(vlan_tag_t);
    }

    switch (type) {
    case ETH_P_ARP:
        arp_receive(buf);
        break;
    case ETH_P_IP:
        ip_receive(buf);
        break;
    default:
        break;
    }
}

void ethernet_print_mac(mac_addr_t mac) {
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
        mac.bytes[0], mac.bytes[1], mac.bytes[2],
        mac.bytes[3], mac.bytes[4], mac.bytes[5]);
}
