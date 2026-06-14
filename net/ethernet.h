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

/* ---- VLAN Management (802.1Q) ---- */

#define VLAN_MAX         64
#define VLAN_ID_MAX      4094

/* PCP values (802.1p priority) */
#define VLAN_PCP_BK       1    /* Background */
#define VLAN_PCP_BE       0    /* Best Effort (default) */
#define VLAN_PCP_EE       2    /* Excellent Effort */
#define VLAN_PCP_CA       3    /* Critical Applications */
#define VLAN_PCP_VI       4    /* Video */
#define VLAN_PCP_VO       5    /* Voice */
#define VLAN_PCP_IC       6    /* Internetwork Control */
#define VLAN_PCP_NC       7    /* Network Control */

typedef struct vlan_interface {
    uint8_t    used;
    uint16_t   vlan_id;
    uint8_t    pcp;              /* 802.1p priority */
    uint8_t    dei;              /* drop eligible indicator */
    net_interface_t *parent;     /* physical interface */
    char       name[16];         /* e.g. "eth0.100" */
    uint32_t   tx_packets;
    uint32_t   rx_packets;
    uint32_t   tx_bytes;
    uint32_t   rx_bytes;
    uint32_t   tx_errors;
    uint32_t   rx_errors;
    mac_addr_t mac;              /* same as parent for now */
    uint8_t    up;
} vlan_interface_t;

/* VLAN filter: allow/deny specific VLAN IDs on an interface */
typedef struct {
    uint16_t vlan_ids[16];       /* allowed VLAN IDs */
    uint8_t  count;
    uint8_t  default_policy;     /* 0 = deny all, 1 = allow all */
} vlan_filter_t;

typedef struct {
    uint32_t vlan_created;
    uint32_t vlan_deleted;
    uint32_t vlan_tagged_sent;
    uint32_t vlan_tagged_rcvd;
    uint32_t vlan_untagged_rcvd;
    uint32_t vlan_filter_drops;
} vlan_stats_t;

int  vlan_create(uint16_t vlan_id, net_interface_t *parent, const char *name);
int  vlan_delete(uint16_t vlan_id);
vlan_interface_t *vlan_get(uint16_t vlan_id);
int  vlan_filter_add(uint16_t vlan_id);
int  vlan_filter_remove(uint16_t vlan_id);
int  vlan_filter_test(uint16_t vlan_id);
void vlan_filter_set_default(int allow);
void vlan_filter_flush(void);
void vlan_init(void);
void vlan_receive(net_buffer_t *buf, uint16_t vlan_id);
const vlan_stats_t *vlan_get_stats(void);
uint32_t vlan_count(void);

/* ---- Network Bridge (802.1D) ---- */

#define BRIDGE_MAX_PORTS    8
#define BRIDGE_FDB_MAX      256       /* forwarding database entries */
#define BRIDGE_FDB_AGE_MS   300000    /* 5 min MAC aging */
#define BRIDGE_STP_HELLO_MS 2000      /* STP hello interval */

/* STP port states */
#define BRIDGE_STP_DISABLED    0
#define BRIDGE_STP_BLOCKING    1
#define BRIDGE_STP_LISTENING   2
#define BRIDGE_STP_LEARNING    3
#define BRIDGE_STP_FORWARDING  4

/* STP port roles */
#define BRIDGE_STP_ROLE_ROOT       0
#define BRIDGE_STP_ROLE_DESIGNATED 1
#define BRIDGE_STP_ROLE_ALT        2
#define BRIDGE_STP_ROLE_BACKUP     3

/* Bridge ID (8 bytes for STP) */
typedef struct __attribute__((packed)) {
    uint16_t    priority;
    mac_addr_t  mac;
} bridge_id_t;

/* Forwarding database entry (MAC → port) */
typedef struct bridge_fdb_entry {
    uint8_t     used;
    mac_addr_t  mac;
    uint8_t     port;             /* port index */
    uint32_t    age_ms;           /* last seen timestamp */
    uint8_t     is_static;        /* 1 = static, 0 = dynamic */
    struct bridge_fdb_entry *next;
} bridge_fdb_entry_t;

/* Bridge port */
typedef struct bridge_port {
    uint8_t            used;
    uint8_t            index;
    net_interface_t   *iface;
    uint8_t            stp_state;    /* BRIDGE_STP_* */
    uint8_t            stp_role;     /* BRIDGE_STP_ROLE_* */
    uint32_t           priority;
    uint32_t           path_cost;
    uint32_t           tx_packets;
    uint32_t           rx_packets;
    uint32_t           tx_bytes;
    uint32_t           rx_bytes;
    uint32_t           tx_dropped;
} bridge_port_t;

/* Bridge instance */
typedef struct network_bridge {
    uint8_t     used;
    char        name[16];
    bridge_id_t id;
    uint32_t    max_age;
    uint32_t    hello_time;
    uint32_t    forward_delay;
    uint32_t    root_path_cost;
    bridge_id_t root_id;
    uint8_t     root_port;
    bridge_port_t ports[BRIDGE_MAX_PORTS];
    uint8_t     port_count;
    bridge_fdb_entry_t *fdb[BRIDGE_FDB_MAX];
    uint32_t    fdb_count;
    uint32_t    total_learned;
    uint32_t    total_aged;
    uint32_t    total_flooded;
    mac_addr_t  mac;              /* bridge MAC (lowest port MAC) */
} network_bridge_t;

#define BRIDGE_MAX 4

typedef struct {
    uint32_t bridges_active;
    uint32_t frames_forwarded;
    uint32_t frames_flooded;
    uint32_t frames_dropped;
    uint32_t mac_learned;
    uint32_t mac_aged;
    uint32_t stp_changes;
} bridge_stats_t;

int  bridge_create(const char *name);
int  bridge_delete(const char *name);
network_bridge_t *bridge_get(const char *name);
int  bridge_add_port(const char *name, net_interface_t *iface);
int  bridge_del_port(const char *name, net_interface_t *iface);
int  bridge_fdb_add(const char *name, mac_addr_t mac, uint8_t port, int is_static);
int  bridge_fdb_del(const char *name, mac_addr_t mac);
void bridge_fdb_age(const char *name, uint32_t now_ms);
void bridge_receive(const char *name, net_buffer_t *buf, uint8_t port);
void bridge_stp_tick(const char *name, uint32_t now_ms);
void bridge_init(void);
void bridge_tick(uint32_t now_ms);
const bridge_stats_t *bridge_get_stats(void);

#endif
