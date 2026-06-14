#ifndef ETHERNET_H
#define ETHERNET_H

#include "net.h"

#define ETH_P_VLAN  0x8100   /* 802.1Q VLAN tagging                 */
#define ETH_P_QINQ  0x88A8   /* 802.1ad provider bridging            */

typedef struct {
    mac_addr_t dst;
    mac_addr_t src;
    uint16_t ethertype;
} ethernet_header_t;

typedef struct {
    uint16_t tci;            /* tag control information (PCP+DEI+VID) */
    uint16_t ethertype;      /* encapsulated ethertype              */
} vlan_tag_t;

/* Send a frame with an optional 802.1Q VLAN tag.  Passing vlan_id=0
 * emits a regular (untagged) frame. */
int  ethernet_send_vlan(net_interface_t *iface, mac_addr_t dst,
                        uint16_t type, uint16_t vlan_id,
                        const void *payload, uint32_t len);
int  ethernet_send(net_interface_t *iface, mac_addr_t dst, uint16_t type,
                   const void *payload, uint32_t len);
void ethernet_receive(net_buffer_t *buf);
void ethernet_print_mac(mac_addr_t mac);

#endif
