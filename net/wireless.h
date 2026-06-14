#ifndef WIRELESS_H
#define WIRELESS_H

#include "stdint.h"
#include "net.h"

/* 扫描结果最大数量 */
#define WIFI_SCAN_MAX_RESULTS  32

/* SSID 最大长度 */
#define WIFI_SSID_MAX_LEN      32

/* 安全类型 */
#define WIFI_SECURITY_OPEN     0
#define WIFI_SECURITY_WEP      1
#define WIFI_SECURITY_WPA      2
#define WIFI_SECURITY_WPA2     3
#define WIFI_SECURITY_WPA3     4

/* WiFi 扫描结果 */
typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t channel;
    int8_t rssi;
    uint8_t security;
    uint32_t beacon_interval;
} wifi_scan_result_t;

/* WiFi 接口 */
typedef struct wifi_interface {
    uint8_t mac[6];
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t connected;
    uint8_t security;
    char passphrase[64];
    uint32_t capabilities;
    wifi_scan_result_t scan_results[WIFI_SCAN_MAX_RESULTS];
    uint32_t scan_count;
    net_interface_t *net_iface;
} wifi_interface_t;

/* 初始化 */
void wifi_init(void);

/* 扫描 */
int wifi_scan(wifi_scan_result_t *results, int max_results);

/* 连接 */
int wifi_connect(const char *ssid, const char *passphrase);
int wifi_connect_bssid(const uint8_t *bssid, const char *passphrase);
void wifi_disconnect(void);

/* 状态 */
int wifi_is_connected(void);
int wifi_get_signal_strength(void);
const char *wifi_get_ssid(void);

/* 安全 */
int wifi_set_wpa2_key(const char *passphrase);
int wifi_wps_push_button(void);

/* 802.11 帧处理 */
int wifi_send_frame(const void *data, uint16_t len);
int wifi_receive_frame(const void *data, uint16_t len);

/* 省电模式 */
void wifi_power_save_enable(void);
void wifi_power_save_disable(void);

/* 信息 */
void wifi_get_info(char *buf, int bufsize);

#endif /* WIRELESS_H */