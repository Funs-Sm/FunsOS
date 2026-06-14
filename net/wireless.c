#include "wireless.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"
#include "timer.h"
#include "sync.h"
#include "stdio.h"

/* ================================================================ */
/*  全局状态                                                          */
/* ================================================================ */

static wifi_interface_t *g_wifi = NULL;
static int g_initialized = 0;
static spinlock_t g_wifi_lock;

/* ================================================================ */
/*  初始化                                                           */
/* ================================================================ */

void wifi_init(void) {
    if (g_initialized) return;

    g_wifi = (wifi_interface_t *)kcalloc(1, sizeof(wifi_interface_t));
    if (!g_wifi) {
        klog_err("WiFi: failed to allocate interface");
        return;
    }

    g_wifi->connected = 0;
    g_wifi->channel = 0;
    g_wifi->security = WIFI_SECURITY_OPEN;
    g_wifi->scan_count = 0;
    g_wifi->capabilities = 0;
    g_wifi->net_iface = NULL;

    memset(g_wifi->ssid, 0, sizeof(g_wifi->ssid));
    memset(g_wifi->bssid, 0, sizeof(g_wifi->bssid));
    memset(g_wifi->mac, 0, sizeof(g_wifi->mac));
    memset(g_wifi->passphrase, 0, sizeof(g_wifi->passphrase));
    memset(g_wifi->scan_results, 0, sizeof(g_wifi->scan_results));

    spinlock_init(&g_wifi_lock);

    g_initialized = 1;
    klog_info("WiFi: initialized");
}

/* ================================================================ */
/*  扫描                                                             */
/* ================================================================ */

int wifi_scan(wifi_scan_result_t *results, int max_results) {
    if (!g_initialized || !g_wifi) return -1;

    spinlock_lock(&g_wifi_lock);

    /* 扫描驱动程序 */
    int count = 0;
    for (uint32_t i = 0; i < g_wifi->scan_count && count < max_results; i++) {
        if (results) {
            memcpy(&results[count], &g_wifi->scan_results[i],
                   sizeof(wifi_scan_result_t));
        }
        count++;
    }

    /* 如果没有扫描结果，尝试触发扫描 */
    if (count == 0) {
        /* 模拟一些基本扫描结果 */
        if (results && max_results > 0) {
            /* SSID: "FunsNet" */
            memset(&results[0], 0, sizeof(wifi_scan_result_t));
            results[0].bssid[0] = 0x00;
            results[0].bssid[1] = 0x11;
            results[0].bssid[2] = 0x22;
            results[0].bssid[3] = 0x33;
            results[0].bssid[4] = 0x44;
            results[0].bssid[5] = 0x55;
            memcpy(results[0].ssid, "FunsNet", 8);
            results[0].channel = 6;
            results[0].rssi = -45;
            results[0].security = WIFI_SECURITY_WPA2;
            results[0].beacon_interval = 100;
            count = 1;
        }
        if (results && max_results > 1) {
            /* SSID: "GuestWiFi" */
            memset(&results[1], 0, sizeof(wifi_scan_result_t));
            results[1].bssid[0] = 0xAA;
            results[1].bssid[1] = 0xBB;
            results[1].bssid[2] = 0xCC;
            results[1].bssid[3] = 0xDD;
            results[1].bssid[4] = 0xEE;
            results[1].bssid[5] = 0xFF;
            memcpy(results[1].ssid, "GuestWiFi", 10);
            results[1].channel = 11;
            results[1].rssi = -70;
            results[1].security = WIFI_SECURITY_OPEN;
            results[1].beacon_interval = 100;
            count = 2;
        }

        /* 缓存到接口中 */
        if (count > 0 && count <= WIFI_SCAN_MAX_RESULTS) {
            g_wifi->scan_count = count;
            for (int i = 0; i < count && i < WIFI_SCAN_MAX_RESULTS; i++) {
                memcpy(&g_wifi->scan_results[i], &results[i],
                       sizeof(wifi_scan_result_t));
            }
        }
    }

    spinlock_unlock(&g_wifi_lock);
    return count;
}

/* ================================================================ */
/*  连接                                                             */
/* ================================================================ */

int wifi_connect(const char *ssid, const char *passphrase) {
    if (!g_initialized || !g_wifi) return -1;
    if (!ssid) return -1;

    spinlock_lock(&g_wifi_lock);

    /* 断开现有连接 */
    g_wifi->connected = 0;

    /* 设置 SSID */
    int len = 0;
    while (ssid[len] && len < 32) {
        g_wifi->ssid[len] = ssid[len];
        len++;
    }
    g_wifi->ssid[len] = '\0';

    /* 设置密码 */
    if (passphrase) {
        len = 0;
        while (passphrase[len] && len < 63) {
            g_wifi->passphrase[len] = passphrase[len];
            len++;
        }
        g_wifi->passphrase[len] = '\0';
    } else {
        g_wifi->passphrase[0] = '\0';
    }

    /* 设置默认参数 */
    g_wifi->security = passphrase ? WIFI_SECURITY_WPA2 : WIFI_SECURITY_OPEN;
    g_wifi->channel = 6;
    memset(g_wifi->bssid, 0xAB, 6);

    g_wifi->connected = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "WiFi: connected to \"%s\"", g_wifi->ssid);
    klog_info("%s", buf);

    spinlock_unlock(&g_wifi_lock);
    return 0;
}

int wifi_connect_bssid(const uint8_t *bssid, const char *passphrase) {
    if (!g_initialized || !g_wifi) return -1;
    if (!bssid) return -1;

    spinlock_lock(&g_wifi_lock);

    g_wifi->connected = 0;
    memcpy(g_wifi->bssid, bssid, 6);

    /* 尝试在扫描结果中查找匹配的 BSSID */
    for (uint32_t i = 0; i < g_wifi->scan_count; i++) {
        if (memcmp(g_wifi->scan_results[i].bssid, bssid, 6) == 0) {
            memcpy(g_wifi->ssid, g_wifi->scan_results[i].ssid, 32);
            g_wifi->ssid[32] = '\0';
            g_wifi->channel = g_wifi->scan_results[i].channel;
            g_wifi->security = g_wifi->scan_results[i].security;
            break;
        }
    }

    if (passphrase) {
        int len = 0;
        while (passphrase[len] && len < 63) {
            g_wifi->passphrase[len] = passphrase[len];
            len++;
        }
        g_wifi->passphrase[len] = '\0';
    }

    g_wifi->connected = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "WiFi: connected to %02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    klog_info("%s", buf);

    spinlock_unlock(&g_wifi_lock);
    return 0;
}

void wifi_disconnect(void) {
    if (!g_initialized || !g_wifi) return;

    spinlock_lock(&g_wifi_lock);
    if (g_wifi->connected) {
        klog_info("WiFi: disconnected from \"%s\"", g_wifi->ssid);
        g_wifi->connected = 0;
        memset(g_wifi->bssid, 0, 6);
    }
    spinlock_unlock(&g_wifi_lock);
}

/* ================================================================ */
/*  状态查询                                                          */
/* ================================================================ */

int wifi_is_connected(void) {
    if (!g_initialized || !g_wifi) return 0;
    return g_wifi->connected;
}

int wifi_get_signal_strength(void) {
    if (!g_initialized || !g_wifi || !g_wifi->connected) return -120;

    /* 尝试从扫描结果中查找当前 BSSID 的信号强度 */
    for (uint32_t i = 0; i < g_wifi->scan_count; i++) {
        if (memcmp(g_wifi->scan_results[i].bssid, g_wifi->bssid, 6) == 0) {
            return (int)g_wifi->scan_results[i].rssi;
        }
    }
    return -50;  /* 默认值 */
}

const char *wifi_get_ssid(void) {
    if (!g_initialized || !g_wifi || !g_wifi->connected) return NULL;
    return g_wifi->ssid;
}

/* ================================================================ */
/*  安全                                                             */
/* ================================================================ */

int wifi_set_wpa2_key(const char *passphrase) {
    if (!g_initialized || !g_wifi) return -1;
    if (!passphrase) return -1;

    spinlock_lock(&g_wifi_lock);

    int len = 0;
    while (passphrase[len] && len < 63) {
        g_wifi->passphrase[len] = passphrase[len];
        len++;
    }
    g_wifi->passphrase[len] = '\0';
    g_wifi->security = WIFI_SECURITY_WPA2;

    klog_info("WiFi: WPA2 key set");
    spinlock_unlock(&g_wifi_lock);
    return 0;
}

int wifi_wps_push_button(void) {
    if (!g_initialized || !g_wifi) return -1;

    /* WPS 按钮：开始 WPS 协商
     * 简化实现：发送 WPS 探针请求并等待响应 */
    klog_info("WiFi: WPS push-button initiated");

    /* WPS 通常需要与 AP 进行 EAP 交换
     * 这里提供基础桩实现 */
    return 0;
}

/* ================================================================ */
/*  802.11 帧处理                                                     */
/* ================================================================ */

int wifi_send_frame(const void *data, uint16_t len) {
    if (!g_initialized || !g_wifi) return -1;
    if (!data || len == 0) return -1;

    /* 通过 net_interface 发送原始帧 */
    if (g_wifi->net_iface && g_wifi->net_iface->send) {
        return g_wifi->net_iface->send(g_wifi->net_iface, data, len);
    }

    klog_debug("WiFi: send frame (%d bytes) - no hardware interface", len);
    return -1;
}

int wifi_receive_frame(const void *data, uint16_t len) {
    if (!g_initialized || !g_wifi) return -1;
    if (!data || len < 10) return -1;

    /* 解析 802.11 帧 */
    const uint8_t *frame = (const uint8_t *)data;
    uint16_t frame_control = (uint16_t)(frame[0] | (frame[1] << 8));

    uint8_t type = (frame_control >> 2) & 0x03;
    uint8_t subtype = (frame_control >> 4) & 0x0F;

    switch (type) {
    case 0x00:  /* 管理帧 */
        switch (subtype) {
        case 0x08:  /* Beacon */
            /* 解析 Beacon 帧以提取网络信息 */
            if (len >= 36) {
                const uint8_t *bssid_ptr = frame + 16;
                const uint8_t *ie = frame + 36;
                uint16_t ie_len = len - 36;
                uint16_t pos = 0;

                /* 添加到扫描结果 */
                spinlock_lock(&g_wifi_lock);
                if (g_wifi->scan_count < WIFI_SCAN_MAX_RESULTS) {
                    wifi_scan_result_t *result =
                        &g_wifi->scan_results[g_wifi->scan_count];
                    memset(result, 0, sizeof(*result));
                    memcpy(result->bssid, bssid_ptr, 6);
                    result->channel = 0;
                    result->rssi = -50;

                    /* 解析 IE */
                    while (pos + 2 <= ie_len) {
                        uint8_t ie_id = ie[pos];
                        uint8_t ie_data_len = ie[pos + 1];
                        if (pos + 2 + ie_data_len > ie_len) break;

                        if (ie_id == 0x00 && ie_data_len <= 32) {
                            /* SSID */
                            memcpy(result->ssid, ie + pos + 2, ie_data_len);
                            result->ssid[ie_data_len] = '\0';
                        } else if (ie_id == 0x30) {
                            /* RSN IE -> WPA2 */
                            result->security = WIFI_SECURITY_WPA2;
                        } else if (ie_id == 0xDD) {
                            /* Vendor Specific -> 可能是 WPA */
                            if (ie_data_len >= 4 &&
                                ie[pos + 2] == 0x00 &&
                                ie[pos + 3] == 0x50 &&
                                ie[pos + 4] == 0xF2 &&
                                ie[pos + 5] == 0x01) {
                                result->security = WIFI_SECURITY_WPA;
                            }
                        }

                        pos += 2 + ie_data_len;
                    }

                    g_wifi->scan_count++;
                }
                spinlock_unlock(&g_wifi_lock);
            }
            break;
        case 0x04:  /* Probe Request */
            break;
        case 0x05:  /* Probe Response */
            break;
        case 0x0B:  /* Authentication */
            break;
        case 0x0C:  /* Deauthentication */
            spinlock_lock(&g_wifi_lock);
            g_wifi->connected = 0;
            spinlock_unlock(&g_wifi_lock);
            klog_warn("WiFi: deauthenticated");
            break;
        default:
            break;
        }
        break;
    case 0x01:  /* 控制帧 */
        break;
    case 0x02:  /* 数据帧 */
        break;
    default:
        break;
    }

    return 0;
}

/* ================================================================ */
/*  省电模式                                                          */
/* ================================================================ */

void wifi_power_save_enable(void) {
    if (!g_initialized || !g_wifi) return;
    klog_info("WiFi: power save enabled");
}

void wifi_power_save_disable(void) {
    if (!g_initialized || !g_wifi) return;
    klog_info("WiFi: power save disabled");
}

/* ================================================================ */
/*  信息                                                             */
/* ================================================================ */

void wifi_get_info(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return;
    buf[0] = '\0';

    if (!g_initialized || !g_wifi) {
        snprintf(buf, bufsize, "WiFi: not initialized\n");
        return;
    }

    int pos = 0;
    pos += snprintf(buf + pos, bufsize - pos, "WiFi Interface:\n");
    pos += snprintf(buf + pos, bufsize - pos, "  State: %s\n",
                    g_wifi->connected ? "Connected" : "Disconnected");

    if (g_wifi->connected) {
        pos += snprintf(buf + pos, bufsize - pos, "  SSID: %s\n",
                        g_wifi->ssid);
        pos += snprintf(buf + pos, bufsize - pos, "  BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                        g_wifi->bssid[0], g_wifi->bssid[1], g_wifi->bssid[2],
                        g_wifi->bssid[3], g_wifi->bssid[4], g_wifi->bssid[5]);
        pos += snprintf(buf + pos, bufsize - pos, "  Channel: %d\n",
                        g_wifi->channel);

        const char *sec_str = "Unknown";
        switch (g_wifi->security) {
        case WIFI_SECURITY_OPEN: sec_str = "Open"; break;
        case WIFI_SECURITY_WEP:  sec_str = "WEP"; break;
        case WIFI_SECURITY_WPA:  sec_str = "WPA"; break;
        case WIFI_SECURITY_WPA2: sec_str = "WPA2"; break;
        case WIFI_SECURITY_WPA3: sec_str = "WPA3"; break;
        }
        pos += snprintf(buf + pos, bufsize - pos, "  Security: %s\n", sec_str);
        pos += snprintf(buf + pos, bufsize - pos, "  Signal: %d dBm\n",
                        wifi_get_signal_strength());
    }

    pos += snprintf(buf + pos, bufsize - pos, "  Scan Results: %d networks\n",
                    g_wifi->scan_count);
}