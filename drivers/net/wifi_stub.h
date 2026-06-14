#ifndef WIFI_STUB_H
#define WIFI_STUB_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- WiFi 接口最大数量 ---- */
#define WIFI_MAX_INTERFACES         4

/* ---- 802.11 常量 ---- */
#define WIFI_SSID_MAX_LEN           32
#define WIFI_BSSID_LEN              6
#define WIFI_MAX_CHANNELS_2GHZ      14
#define WIFI_MAX_CHANNELS_5GHZ      200
#define WIFI_MAX_RATES              12

/* ---- 802.11 帧类型 ---- */
#define WIFI_FTYPE_MGMT             0x00    /* Management Frame               */
#define WIFI_FTYPE_CTRL             0x01    /* Control Frame                  */
#define WIFI_FTYPE_DATA             0x02    /* Data Frame                     */
#define WIFI_FTYPE_EXT              0x03    /* Extension Frame                */

/* ---- 802.11 管理帧子类型 ---- */
#define WIFI_STYPE_ASSOC_REQ        0x00    /* Association Request            */
#define WIFI_STYPE_ASSOC_RESP       0x01    /* Association Response           */
#define WIFI_STYPE_REASSOC_REQ      0x02    /* Reassociation Request          */
#define WIFI_STYPE_REASSOC_RESP     0x03    /* Reassociation Response         */
#define WIFI_STYPE_PROBE_REQ        0x04    /* Probe Request                  */
#define WIFI_STYPE_PROBE_RESP       0x05    /* Probe Response                 */
#define WIFI_STYPE_BEACON           0x08    /* Beacon                         */
#define WIFI_STYPE_ATIM             0x09    /* ATIM                           */
#define WIFI_STYPE_DISASSOC         0x0A    /* Disassociation                 */
#define WIFI_STYPE_AUTH             0x0B    /* Authentication                 */
#define WIFI_STYPE_DEAUTH           0x0C    /* Deauthentication               */
#define WIFI_STYPE_ACTION           0x0D    /* Action                         */

/* ---- 802.11 控制帧子类型 ---- */
#define WIFI_STYPE_CTRL_ACK         0x0D    /* Acknowledgment                  */
#define WIFI_STYPE_CTRL_RTS         0x0B    /* Request to Send                */
#define WIFI_STYPE_CTRL_CTS         0x0C    /* Clear to Send                  */

/* ---- 802.11 数据帧子类型 ---- */
#define WIFI_STYPE_DATA             0x00    /* Data                           */
#define WIFI_STYPE_DATA_CFACK       0x01    /* Data + CF-Ack                  */
#define WIFI_STYPE_DATA_CFPOLL      0x02    /* Data + CF-Poll                 */
#define WIFI_STYPE_DATA_CFACKPOLL   0x03    /* Data + CF-Ack + CF-Poll        */
#define WIFI_STYPE_NULL             0x04    /* Null (no data)                 */
#define WIFI_STYPE_QOS_DATA         0x08    /* QoS Data                       */
#define WIFI_STYPE_QOS_NULL         0x0C    /* QoS Null                       */

/* ---- 安全类型 ---- */
#define WIFI_SECURITY_NONE          0x00    /* Open / No security             */
#define WIFI_SECURITY_WEP           0x01    /* WEP (Wired Equivalent Privacy) */
#define WIFI_SECURITY_WPA           0x02    /* WPA (Wi-Fi Protected Access) 1 */
#define WIFI_SECURITY_WPA2          0x03    /* WPA2 (802.11i / RSN)           */
#define WIFI_SECURITY_WPA3          0x04    /* WPA3 (SAE / OWE)               */
#define WIFI_SECURITY_WPA2_ENT      0x05    /* WPA2-Enterprise (802.1X)       */
#define WIFI_SECURITY_WPA3_ENT      0x06    /* WPA3-Enterprise                */

/* ---- 加密套件 ---- */
#define WIFI_CIPHER_NONE            0x00
#define WIFI_CIPHER_WEP40           0x01
#define WIFI_CIPHER_TKIP            0x02
#define WIFI_CIPHER_CCMP            0x04    /* CCMP-128 (AES)                 */
#define WIFI_CIPHER_WEP104          0x05
#define WIFI_CIPHER_GCMP            0x08    /* GCMP-128                       */
#define WIFI_CIPHER_GCMP256         0x09    /* GCMP-256                       */
#define WIFI_CIPHER_CCMP256         0x0A    /* CCMP-256                       */

/* ---- 密钥管理套件 ---- */
#define WIFI_AKM_NONE               0x00
#define WIFI_AKM_8021X              0x01
#define WIFI_AKM_PSK                0x02
#define WIFI_AKM_SAE                0x04    /* Simultaneous Auth of Equals    */
#define WIFI_AKM_FT_PSK             0x08    /* Fast BSS Transition PSK        */
#define WIFI_AKM_FT_8021X           0x10    /* Fast BSS Transition 802.1X     */
#define WIFI_AKM_OWE                0x20    /* Opportunistic Wireless Encrypt */

/* ---- 频道定义 ---- */
/* 2.4 GHz 频道 (1-14) */
#define WIFI_CHAN_2GHZ_FIRST        1
#define WIFI_CHAN_2GHZ_LAST         14
#define WIFI_CHAN_2GHZ_FREQ(n)      (2412 + ((n) - 1) * 5)

/* 5 GHz 频道 (36-165) */
#define WIFI_CHAN_5GHZ_FIRST        36
#define WIFI_CHAN_5GHZ_LAST         165

/* ---- 802.11 信息元素 ID ---- */
#define WIFI_IE_SSID                0x00    /* SSID                           */
#define WIFI_IE_SUPPORTED_RATES     0x01    /* Supported Rates                */
#define WIFI_IE_DS_PARAM            0x03    /* DSSS Parameter Set             */
#define WIFI_IE_TIM                 0x05    /* Traffic Indication Map         */
#define WIFI_IE_COUNTRY             0x07    /* Country Information            */
#define WIFI_IE_ERP                 0x2A    /* ERP Information                */
#define WIFI_IE_EXT_SUPPORTED_RATES 0x32    /* Extended Supported Rates       */
#define WIFI_IE_HT_CAPABILITIES     0x2D    /* HT Capabilities                */
#define WIFI_IE_RSN                 0x30    /* Robust Security Network        */
#define WIFI_IE_HT_OPERATION        0x3D    /* HT Operation                   */
#define WIFI_IE_VHT_CAPABILITIES    0xBF    /* VHT Capabilities               */
#define WIFI_IE_VHT_OPERATION       0xC0    /* VHT Operation                  */
#define WIFI_IE_WPA                 0xDD    /* Vendor Specific (WPA)          */

/* ---- 802.11 帧控制域 ---- */
#define WIFI_FC_PROTOCOL_VERSION    0x00    /* Protocol Version               */
#define WIFI_FC_TYPE_MASK           0x0C    /* Type Mask                      */
#define WIFI_FC_TYPE_SHIFT          2       /* Type Shift                     */
#define WIFI_FC_STYPE_MASK          0xF0    /* Subtype Mask                   */
#define WIFI_FC_STYPE_SHIFT         4       /* Subtype Shift                  */
#define WIFI_FC_TO_DS               (1 << 8)  /* To Distribution System        */
#define WIFI_FC_FROM_DS             (1 << 9)  /* From Distribution System      */
#define WIFI_FC_MORE_FRAG           (1 << 10) /* More Fragments               */
#define WIFI_FC_RETRY               (1 << 11) /* Retry                         */
#define WIFI_FC_PWR_MGMT            (1 << 12) /* Power Management              */
#define WIFI_FC_MORE_DATA           (1 << 13) /* More Data                     */
#define WIFI_FC_PROTECTED           (1 << 14) /* Protected Frame               */
#define WIFI_FC_ORDER               (1 << 15) /* Order                         */

/* ---- 连接状态机 ---- */
typedef enum {
    WIFI_STATE_DOWN = 0,          /* Interface down                 */
    WIFI_STATE_SCANNING,          /* Scanning for networks          */
    WIFI_STATE_AUTHENTICATING,    /* Authenticating with AP         */
    WIFI_STATE_ASSOCIATING,       /* Associating with AP            */
    WIFI_STATE_4WAY_HANDSHAKE,    /* 4-way handshake in progress    */
    WIFI_STATE_CONNECTED,         /* Connected and operational      */
    WIFI_STATE_DISCONNECTING,     /* Disconnecting                  */
    WIFI_STATE_ERROR              /* Error state                    */
} wifi_state_t;

/* ---- 扫描结果结构 ---- */
typedef struct {
    uint8_t  bssid[WIFI_BSSID_LEN]; /* BSSID / AP MAC address        */
    uint8_t  ssid[WIFI_SSID_MAX_LEN + 1]; /* SSID (null-terminated)   */
    uint8_t  ssid_len;              /* SSID length                    */
    uint8_t  channel;               /* Channel number                 */
    uint16_t freq;                  /* Frequency in MHz               */
    int8_t   rssi;                  /* Signal strength (dBm)          */
    uint8_t  security;              /* Security type                  */
    uint8_t  cipher;                /* Cipher suite                   */
    uint8_t  akm;                   /* AKM suite                      */
    uint16_t capability;            /* Capability info field          */
    uint8_t  supported_rates[WIFI_MAX_RATES]; /* Rates in 500 kbps units */
    uint8_t  num_rates;             /* Number of supported rates      */
    uint8_t  ht_supported;          /* HT (802.11n) supported         */
    uint8_t  vht_supported;         /* VHT (802.11ac) supported       */
    uint8_t  wpa3_supported;        /* WPA3 supported                 */
    uint32_t max_rate;              /* Maximum data rate (kbps)       */
} wifi_scan_result_t;

/* ---- 802.11 帧头结构 (管理帧) ---- */
typedef struct __attribute__((packed)) {
    uint16_t frame_control;         /* Frame Control Field            */
    uint16_t duration;              /* Duration / ID                  */
    uint8_t  addr1[WIFI_BSSID_LEN]; /* Address 1 (Destination)        */
    uint8_t  addr2[WIFI_BSSID_LEN]; /* Address 2 (Source)             */
    uint8_t  addr3[WIFI_BSSID_LEN]; /* Address 3 (BSSID)              */
    uint16_t seq_ctrl;              /* Sequence Control               */
    uint8_t  addr4[WIFI_BSSID_LEN]; /* Address 4 (optional)           */
} wifi_80211_mgmt_hdr_t;

/* ---- 802.11 帧头结构 (数据帧) ---- */
typedef struct __attribute__((packed)) {
    uint16_t frame_control;         /* Frame Control Field            */
    uint16_t duration;              /* Duration / ID                  */
    uint8_t  addr1[WIFI_BSSID_LEN]; /* Address 1 (Receiver)           */
    uint8_t  addr2[WIFI_BSSID_LEN]; /* Address 2 (Transmitter)        */
    uint8_t  addr3[WIFI_BSSID_LEN]; /* Address 3 (BSSID / DA/SA)      */
    uint16_t seq_ctrl;              /* Sequence Control               */
    uint8_t  addr4[WIFI_BSSID_LEN]; /* Address 4 (optional)           */
    uint16_t qos_ctrl;              /* QoS Control (802.11e)          */
    /* 之后是 LLC/SNAP 或直接的 IP 数据 */
    uint8_t  payload[0];            /* Payload (variable)             */
} wifi_80211_data_hdr_t;

/* ---- 802.11 信息元素结构 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  id;                    /* Element ID                     */
    uint8_t  len;                   /* Data length                    */
    uint8_t  data[0];               /* Variable length data           */
} wifi_ie_t;

/* ---- RSN IE 结构 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  id;                    /* IE ID = 0x30 (RSN)             */
    uint8_t  len;                   /* Length                         */
    uint16_t version;               /* RSN Version                    */
    uint32_t group_cipher;          /* Group Cipher Suite OUI/Type    */
    uint16_t pairwise_count;        /* Pairwise Cipher Suite Count    */
    uint32_t pairwise_ciphers[4];   /* Pairwise Cipher Suites         */
    uint16_t akm_count;             /* AKM Suite Count                */
    uint32_t akm_suites[4];         /* AKM Suites                     */
    uint16_t rsn_capabilities;      /* RSN Capabilities               */
    uint16_t pmkid_count;           /* PMKID Count                    */
    /* PMKID list follows */
} wifi_rsn_ie_t;

/* ---- WiFi 设备接口 ---- */
typedef struct wifi_interface wifi_interface_t;

/* 回调函数指针 */
typedef int  (*wifi_scan_fn_t)(wifi_interface_t *wiface, uint8_t channel);
typedef int  (*wifi_connect_fn_t)(wifi_interface_t *wiface, const uint8_t *ssid,
                                  uint8_t ssid_len, const uint8_t *bssid,
                                  uint8_t security, const char *password);
typedef int  (*wifi_disconnect_fn_t)(wifi_interface_t *wiface);
typedef int  (*wifi_auth_fn_t)(wifi_interface_t *wiface, const uint8_t *bssid,
                               uint8_t security);
typedef int  (*wifi_set_channel_fn_t)(wifi_interface_t *wiface, uint8_t channel);
typedef int  (*wifi_set_key_fn_t)(wifi_interface_t *wiface, uint8_t key_idx,
                                  const uint8_t *key, uint8_t key_len,
                                  uint8_t cipher);
typedef int  (*wifi_get_stats_fn_t)(wifi_interface_t *wiface, void *stats);

struct wifi_interface {
    char name[16];                          /* Interface name               */
    uint8_t mac[WIFI_BSSID_LEN];            /* MAC address (BSSID)          */
    wifi_state_t state;                     /* Current connection state     */
    net_interface_t *net_iface;             /* Linked net_interface         */

    /* BSS info */
    uint8_t  bssid[WIFI_BSSID_LEN];         /* Associated BSSID             */
    uint8_t  ssid[WIFI_SSID_MAX_LEN + 1];   /* SSID                         */
    uint8_t  ssid_len;                      /* SSID length                  */
    uint8_t  channel;                       /* Current channel              */
    uint16_t freq;                          /* Current frequency (MHz)      */
    uint8_t  security;                      /* Security type                */
    uint8_t  cipher;                        /* Active cipher suite          */
    uint8_t  akm;                           /* Active AKM suite             */

    /* Scan results */
    wifi_scan_result_t scan_results[32];    /* Last scan results            */
    uint8_t  num_scan_results;              /* Number of scan results       */

    /* Capabilities */
    uint8_t  supports_2ghz;                 /* 2.4 GHz band support         */
    uint8_t  supports_5ghz;                 /* 5 GHz band support           */
    uint8_t  supports_ht;                   /* 802.11n (HT) support         */
    uint8_t  supports_vht;                  /* 802.11ac (VHT) support       */
    uint8_t  supports_wpa3;                 /* WPA3 support                 */
    uint32_t max_tx_power;                  /* Max TX power (dBm)           */

    /* Hardware-specific callbacks */
    wifi_scan_fn_t         scan;            /* Start scan                   */
    wifi_connect_fn_t      connect;         /* Connect to AP                */
    wifi_disconnect_fn_t   disconnect;      /* Disconnect from AP           */
    wifi_auth_fn_t         authenticate;    /* Authenticate                 */
    wifi_set_channel_fn_t  set_channel;     /* Set channel                  */
    wifi_set_key_fn_t      set_key;         /* Set encryption key           */
    wifi_get_stats_fn_t    get_stats;       /* Get interface statistics     */

    /* Driver private data */
    void *driver_data;
};

/* ---- 公共 API ---- */

/* 注册一个新 WiFi 接口 */
int wifi_register_interface(wifi_interface_t *wiface);

/* 注销一个 WiFi 接口 */
void wifi_unregister_interface(wifi_interface_t *wiface);

/* 获取 WiFi 接口 */
wifi_interface_t *wifi_get_interface(uint32_t index);
wifi_interface_t *wifi_get_interface_by_name(const char *name);

/* 发起扫描 */
int wifi_scan(wifi_interface_t *wiface, uint8_t channel);

/* 连接到 AP */
int wifi_connect(wifi_interface_t *wiface, const uint8_t *ssid,
                 uint8_t ssid_len, const uint8_t *bssid,
                 uint8_t security, const char *password);

/* 断开连接 */
int wifi_disconnect(wifi_interface_t *wiface);

/* 获取扫描结果 */
uint8_t wifi_get_scan_results(wifi_interface_t *wiface,
                              wifi_scan_result_t *results, uint8_t max_results);

/* 获取连接状态 */
wifi_state_t wifi_get_state(wifi_interface_t *wiface);

/* 获取信号强度 */
int8_t wifi_get_rssi(wifi_interface_t *wiface);

/* 工具函数 */
uint16_t wifi_channel_to_freq(uint8_t channel);
uint8_t  wifi_freq_to_channel(uint16_t freq);
const char *wifi_security_to_string(uint8_t security);
const char *wifi_state_to_string(wifi_state_t state);

/* 初始化 WiFi 子系统 */
void wifi_init(void);

#endif /* WIFI_STUB_H */