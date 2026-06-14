/*
 * wifi_stub.c - WiFi 驱动框架/存根
 *
 * 提供抽象的 WiFi 设备接口, 包括:
 * - 802.11 帧结构定义
 * - 频道和频率转换
 * - 扫描/连接/断开/认证接口
 * - 连接状态机
 * - 安全类型管理
 *
 * 本文件不包含硬件特定的代码, 作为未来硬件特定 WiFi 驱动的框架。
 */

#include "wifi_stub.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

/* ---- 私有状态 ---- */
static wifi_interface_t *wifi_interfaces[WIFI_MAX_INTERFACES];
static uint32_t wifi_interface_count = 0;

/* ---- 频道/频率转换工具函数 ---- */
uint16_t wifi_channel_to_freq(uint8_t channel) {
    if (channel >= 1 && channel <= 14) {
        /* 2.4 GHz band */
        return WIFI_CHAN_2GHZ_FREQ(channel);
    } else if (channel >= 36 && channel <= 165) {
        /* 5 GHz band (简化: 每隔 5 MHz 增量) */
        uint16_t base = 5000; /* 5 GHz band base */
        uint16_t offset = (channel - 36) * 5;
        return base + offset;
    }
    return 0; /* Invalid */
}

uint8_t wifi_freq_to_channel(uint16_t freq) {
    /* 2.4 GHz band */
    if (freq >= 2412 && freq <= 2484) {
        /* Channels 1-14: 2412 + (ch-1)*5 */
        uint8_t ch = (uint8_t)((freq - 2412) / 5) + 1;
        if (ch >= 1 && ch <= 14) return ch;
    }
    /* 5 GHz band */
    if (freq >= 5000 && freq <= 5900) {
        /* Channels 36-165: 5180 + (ch-36)*5 */
        uint8_t ch = (uint8_t)((freq - 5000) / 5) + 36;
        if (ch >= 36 && ch <= 200) return ch;
    }
    return 0; /* Invalid */
}

const char *wifi_security_to_string(uint8_t security) {
    switch (security) {
        case WIFI_SECURITY_NONE:   return "Open";
        case WIFI_SECURITY_WEP:    return "WEP";
        case WIFI_SECURITY_WPA:    return "WPA";
        case WIFI_SECURITY_WPA2:   return "WPA2";
        case WIFI_SECURITY_WPA3:   return "WPA3";
        case WIFI_SECURITY_WPA2_ENT: return "WPA2-Enterprise";
        case WIFI_SECURITY_WPA3_ENT: return "WPA3-Enterprise";
        default:                   return "Unknown";
    }
}

const char *wifi_state_to_string(wifi_state_t state) {
    switch (state) {
        case WIFI_STATE_DOWN:            return "Down";
        case WIFI_STATE_SCANNING:        return "Scanning";
        case WIFI_STATE_AUTHENTICATING:  return "Authenticating";
        case WIFI_STATE_ASSOCIATING:     return "Associating";
        case WIFI_STATE_4WAY_HANDSHAKE:  return "4-Way Handshake";
        case WIFI_STATE_CONNECTED:       return "Connected";
        case WIFI_STATE_DISCONNECTING:   return "Disconnecting";
        case WIFI_STATE_ERROR:           return "Error";
        default:                         return "Unknown";
    }
}

/* ---- 接口注册/注销 ---- */
int wifi_register_interface(wifi_interface_t *wiface) {
    if (!wiface) return -1;
    if (wifi_interface_count >= WIFI_MAX_INTERFACES) {
        klog_err("wifi: Maximum interfaces reached (%d)", WIFI_MAX_INTERFACES);
        return -1;
    }

    wiface->state             = WIFI_STATE_DOWN;
    wiface->num_scan_results  = 0;
    wiface->ssid_len          = 0;
    memset(wiface->ssid, 0, sizeof(wiface->ssid));
    memset(wiface->bssid, 0, sizeof(wiface->bssid));
    memset(wiface->scan_results, 0, sizeof(wiface->scan_results));

    wifi_interfaces[wifi_interface_count++] = wiface;

    klog_info("wifi: Interface '%s' registered", wiface->name);
    return 0;
}

void wifi_unregister_interface(wifi_interface_t *wiface) {
    uint32_t i;
    for (i = 0; i < wifi_interface_count; i++) {
        if (wifi_interfaces[i] == wiface) {
            /* 将后面的接口前移 */
            uint32_t j;
            for (j = i; j < wifi_interface_count - 1; j++) {
                wifi_interfaces[j] = wifi_interfaces[j + 1];
            }
            wifi_interfaces[wifi_interface_count - 1] = 0;
            wifi_interface_count--;

            klog_info("wifi: Interface '%s' unregistered", wiface->name);
            return;
        }
    }
    klog_warn("wifi: Interface not found for unregistration");
}

/* ---- 接口查询 ---- */
wifi_interface_t *wifi_get_interface(uint32_t index) {
    if (index >= wifi_interface_count) return 0;
    return wifi_interfaces[index];
}

wifi_interface_t *wifi_get_interface_by_name(const char *name) {
    uint32_t i;
    for (i = 0; i < wifi_interface_count; i++) {
        if (strcmp(wifi_interfaces[i]->name, name) == 0) {
            return wifi_interfaces[i];
        }
    }
    return 0;
}

/* ---- 扫描 API ---- */
int wifi_scan(wifi_interface_t *wiface, uint8_t channel) {
    if (!wiface) return -1;
    if (wiface->state != WIFI_STATE_DOWN && wiface->state != WIFI_STATE_CONNECTED) {
        klog_warn("wifi: Cannot scan in state %s", wifi_state_to_string(wiface->state));
        return -1;
    }

    wiface->state = WIFI_STATE_SCANNING;
    wiface->num_scan_results = 0;

    /* 如果有硬件特定回调, 调用它 */
    if (wiface->scan) {
        int ret = wiface->scan(wiface, channel);
        if (ret != 0) {
            wiface->state = WIFI_STATE_DOWN;
            return ret;
        }
    }

    /* 在真实的实现中, 这里会等待硬件完成扫描并填充结果。
     * 作为框架, 扫描结果由硬件驱动填充到 wiface->scan_results。 */

    klog_info("wifi: Scan started on channel %d for '%s'", channel, wiface->name);
    return 0;
}

/* ---- 连接 API ---- */
int wifi_connect(wifi_interface_t *wiface, const uint8_t *ssid,
                 uint8_t ssid_len, const uint8_t *bssid,
                 uint8_t security, const char *password) {
    if (!wiface) return -1;
    if (ssid_len > WIFI_SSID_MAX_LEN) return -1;

    /* 状态检查 */
    if (wiface->state != WIFI_STATE_DOWN) {
        klog_warn("wifi: Already %s, disconnect first", wifi_state_to_string(wiface->state));
        return -1;
    }

    /* 保存连接参数 */
    memcpy(wiface->ssid, ssid, ssid_len);
    wiface->ssid[ssid_len] = '\0';
    wiface->ssid_len       = ssid_len;
    wiface->security       = security;

    if (bssid) {
        memcpy(wiface->bssid, bssid, WIFI_BSSID_LEN);
    }

    /* 状态机转换: DOWN -> AUTHENTICATING */
    wiface->state = WIFI_STATE_AUTHENTICATING;

    /* 如果有硬件回调, 调用 connect */
    if (wiface->connect) {
        int ret = wiface->connect(wiface, ssid, ssid_len, bssid, security, password);
        if (ret != 0) {
            wiface->state = WIFI_STATE_DOWN;
            return ret;
        }
    }

    klog_info("wifi: Connecting to SSID='%s' security=%s on '%s'",
              wiface->ssid, wifi_security_to_string(security), wiface->name);
    return 0;
}

/* ---- 断开连接 API ---- */
int wifi_disconnect(wifi_interface_t *wiface) {
    if (!wiface) return -1;

    if (wiface->state == WIFI_STATE_DOWN) {
        return 0; /* Already disconnected */
    }

    wiface->state = WIFI_STATE_DISCONNECTING;

    /* 如果有硬件回调, 调用 disconnect */
    if (wiface->disconnect) {
        wiface->disconnect(wiface);
    }

    /* 清除连接状态 */
    memset(wiface->ssid, 0, sizeof(wiface->ssid));
    wiface->ssid_len = 0;
    memset(wiface->bssid, 0, sizeof(wiface->bssid));
    wiface->security = WIFI_SECURITY_NONE;
    wiface->cipher   = WIFI_CIPHER_NONE;
    wiface->akm      = WIFI_AKM_NONE;

    wiface->state = WIFI_STATE_DOWN;

    klog_info("wifi: Disconnected from '%s'", wiface->name);
    return 0;
}

/* ---- 扫描结果查询 ---- */
uint8_t wifi_get_scan_results(wifi_interface_t *wiface,
                              wifi_scan_result_t *results, uint8_t max_results) {
    if (!wiface || !results) return 0;

    uint8_t count = wiface->num_scan_results;
    if (count > max_results) count = max_results;

    memcpy(results, wiface->scan_results, count * sizeof(wifi_scan_result_t));
    return count;
}

/* ---- 连接状态查询 ---- */
wifi_state_t wifi_get_state(wifi_interface_t *wiface) {
    if (!wiface) return WIFI_STATE_DOWN;
    return wiface->state;
}

/* ---- 信号强度查询 ---- */
int8_t wifi_get_rssi(wifi_interface_t *wiface) {
    if (!wiface) return -128;
    if (wiface->state != WIFI_STATE_CONNECTED) return -128;

    /* 返回当前 BSSID 的 RSSI (来自扫描结果缓存) */
    uint8_t i;
    for (i = 0; i < wiface->num_scan_results; i++) {
        if (memcmp(wiface->scan_results[i].bssid, wiface->bssid, WIFI_BSSID_LEN) == 0) {
            return wiface->scan_results[i].rssi;
        }
    }
    return -128;  /* 未找到 */
}

/* ---- 填充默认扫描结果 (框架工具函数) ---- */
static void wifi_fill_dummy_scan_result(wifi_scan_result_t *result,
                                        const uint8_t *bssid, const char *ssid,
                                        uint8_t channel, int8_t rssi, uint8_t security) {
    memset(result, 0, sizeof(wifi_scan_result_t));
    memcpy(result->bssid, bssid, WIFI_BSSID_LEN);

    uint8_t ssid_len = 0;
    if (ssid) {
        ssid_len = (uint8_t)strlen(ssid);
        if (ssid_len > WIFI_SSID_MAX_LEN) ssid_len = WIFI_SSID_MAX_LEN;
        memcpy(result->ssid, ssid, ssid_len);
        result->ssid[ssid_len] = '\0';
    }
    result->ssid_len        = ssid_len;
    result->channel         = channel;
    result->freq            = wifi_channel_to_freq(channel);
    result->rssi            = rssi;
    result->security        = security;
    result->cipher          = (security >= WIFI_SECURITY_WPA2) ? WIFI_CIPHER_CCMP
                             : (security == WIFI_SECURITY_WPA)  ? WIFI_CIPHER_TKIP
                             : (security == WIFI_SECURITY_WEP)  ? WIFI_CIPHER_WEP40
                             : WIFI_CIPHER_NONE;
    result->akm             = (security >= WIFI_SECURITY_WPA2) ? WIFI_AKM_PSK
                             : WIFI_AKM_NONE;
    result->capability      = 0x0411;  /* ESS + Privacy + Short Preamble */
    result->ht_supported    = 0;
    result->vht_supported   = 0;
    result->wpa3_supported  = 0;
    result->num_rates       = 4;
    result->supported_rates[0] = 2;   /* 1 Mbps */
    result->supported_rates[1] = 4;   /* 2 Mbps */
    result->supported_rates[2] = 11;  /* 5.5 Mbps */
    result->supported_rates[3] = 22;  /* 11 Mbps */
    result->max_rate        = 11000;  /* 11 Mbps (kbps) */
}

/* ---- 通用 WiFi 接口管理器 ---- */

/* 为使用此框架的每个 WiFi 设备驱动调用 */
void wifi_register_driver(wifi_interface_t *wiface,
                          wifi_scan_fn_t scan_cb,
                          wifi_connect_fn_t connect_cb,
                          wifi_disconnect_fn_t disconnect_cb,
                          wifi_auth_fn_t auth_cb,
                          wifi_set_channel_fn_t set_channel_cb,
                          wifi_set_key_fn_t set_key_cb,
                          wifi_get_stats_fn_t get_stats_cb,
                          uint8_t supports_2ghz, uint8_t supports_5ghz,
                          uint8_t supports_ht, uint8_t supports_vht,
                          uint8_t supports_wpa3, uint32_t max_tx_power) {
    wiface->scan           = scan_cb;
    wiface->connect        = connect_cb;
    wiface->disconnect     = disconnect_cb;
    wiface->authenticate   = auth_cb;
    wiface->set_channel    = set_channel_cb;
    wiface->set_key        = set_key_cb;
    wiface->get_stats      = get_stats_cb;
    wiface->supports_2ghz  = supports_2ghz;
    wiface->supports_5ghz  = supports_5ghz;
    wiface->supports_ht    = supports_ht;
    wiface->supports_vht   = supports_vht;
    wiface->supports_wpa3  = supports_wpa3;
    wiface->max_tx_power   = max_tx_power;

    wifi_register_interface(wiface);
}

/* ---- 发送 802.11 管理帧 (框架工具函数) ---- */
int wifi_send_mgmt_frame(wifi_interface_t *wiface, uint8_t type, uint8_t subtype,
                         const uint8_t *da, const uint8_t *bssid,
                         const uint8_t *payload, uint32_t payload_len) {
    if (!wiface || !wiface->net_iface) return -1;

    /* 构建 802.11 管理帧头 */
    uint8_t frame[2048];
    uint16_t fc = (uint16_t)((type << WIFI_FC_TYPE_SHIFT) |
                             (subtype << WIFI_FC_STYPE_SHIFT));
    memset(frame, 0, sizeof(frame));

    /* 帧控制: 协议版本 0 */
    *(uint16_t *)(frame + 0) = fc;
    /* Duration (简化) */
    *(uint16_t *)(frame + 2) = 0;
    /* Address 1 (DA) */
    memcpy(frame + 4, da, WIFI_BSSID_LEN);
    /* Address 2 (SA = 自己的 MAC) */
    memcpy(frame + 10, wiface->mac, WIFI_BSSID_LEN);
    /* Address 3 (BSSID) */
    memcpy(frame + 16, bssid, WIFI_BSSID_LEN);
    /* Sequence Control */
    *(uint16_t *)(frame + 22) = 0;

    /* 追加 payload */
    uint32_t hdr_len = 24; /* 管理帧头: 24 字节 */
    if (payload && payload_len > 0) {
        memcpy(frame + hdr_len, payload, payload_len);
    }

    /* 通过关联的 net_interface 发送 */
    if (wiface->net_iface->send) {
        return wiface->net_iface->send(wiface->net_iface, frame,
                                       hdr_len + payload_len);
    }
    return -1;
}

/* ---- 辅助: 构建 Probe Request ---- */
int wifi_build_probe_req(wifi_interface_t *wiface, const uint8_t *ssid,
                         uint8_t ssid_len, uint8_t *out_buf, uint32_t *out_len) {
    uint32_t offset = 0;

    /* IE: SSID */
    out_buf[offset++] = WIFI_IE_SSID;
    out_buf[offset++] = ssid_len;
    if (ssid && ssid_len > 0) {
        memcpy(out_buf + offset, ssid, ssid_len);
        offset += ssid_len;
    }

    /* IE: Supported Rates (强制: 1, 2, 5.5, 11 Mbps) */
    uint8_t rates[] = { 0x82, 0x84, 0x8B, 0x96 }; /* 基本速率编码 */
    out_buf[offset++] = WIFI_IE_SUPPORTED_RATES;
    out_buf[offset++] = sizeof(rates);
    memcpy(out_buf + offset, rates, sizeof(rates));
    offset += sizeof(rates);

    *out_len = offset;
    return 0;
}

/* ---- 初始化 WiFi 子系统 ---- */
void wifi_init(void) {
    uint32_t i;
    for (i = 0; i < WIFI_MAX_INTERFACES; i++) {
        wifi_interfaces[i] = 0;
    }
    wifi_interface_count = 0;

    klog_info("wifi: WiFi framework initialized (max %d interfaces)", WIFI_MAX_INTERFACES);
}