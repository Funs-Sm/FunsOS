#ifndef FUNSOS_DRIVER_H
#define FUNSOS_DRIVER_H

#include "stdint.h"
#include "stddef.h"
#include "funsos_power.h"   /* 引入 funsos_cpu_info_t 定义 */

/*
 * FUNSOS SDK - 驱动信息查询子模块
 *
 * 提供对 PCI/USB/块设备等硬件资源的程序化查询接口
 *
 * 版本: 1.0.0 (FunsCore v0.8 新增)
 */

#define FUNSOS_DRIVER_API_VERSION  0x0100

/* ---- 设备类型 ---- */
#define FUNSOS_DEV_PCI     1
#define FUNSOS_DEV_USB     2
#define FUNSOS_DEV_BLOCK   3
#define FUNSOS_DEV_NET     4
#define FUNSOS_DEV_INPUT   5
#define FUNSOS_DEV_AUDIO   6
#define FUNSOS_DEV_GPU     7

/* ---- PCI 设备信息 ---- */
typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  irq;
} funsos_pci_device_t;

/* ---- 块设备信息 ---- */
typedef struct {
    char     name[32];
    uint32_t major;
    uint32_t minor;
    uint64_t size_bytes;
    uint8_t  removable;
} funsos_block_device_t;

/* ---- 网络接口信息 ---- */
typedef struct {
    char     name[16];
    uint8_t  mac[6];
    uint32_t ipv4;
    uint32_t mask;
    uint32_t gateway;
    uint8_t  up;
    uint32_t mtu;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} funsos_net_iface_t;

/* ---- CPU 信息 ---- */
/* funsos_cpu_info_t 已在 funsos_power.h 中定义，此处直接复用 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PCI 设备查询 ---- */

/*
 * 枚举所有 PCI 设备
 * 参数: devices - 接收设备数组的缓冲区; max_count - 缓冲区最大容量
 * 返回: 实际写入的设备数, -1 失败
 */
int funsos_driver_list_pci(funsos_pci_device_t *devices, uint32_t max_count);

/*
 * 获取 PCI 设备总数
 */
int funsos_driver_count_pci(void);

/* ---- 块设备查询 ---- */

/*
 * 枚举所有块设备
 * 参数: devices - 接收设备数组的缓冲区; max_count - 缓冲区最大容量
 * 返回: 实际写入的设备数, -1 失败
 */
int funsos_driver_list_block(funsos_block_device_t *devices, uint32_t max_count);

/* ---- 网络接口查询 ---- */

/*
 * 枚举所有网络接口
 * 参数: ifaces - 接收接口数组的缓冲区; max_count - 缓冲区最大容量
 * 返回: 实际写入的接口数, -1 失败
 */
int funsos_driver_list_net(funsos_net_iface_t *ifaces, uint32_t max_count);

/* ---- CPU 信息 ---- */

/*
 * 获取 CPU 信息
 * 参数: info - 接收 CPU 信息的结构体
 * 返回: 0 成功, -1 失败
 */
int funsos_driver_get_cpu_info(funsos_cpu_info_t *info);

/* ---- 通用驱动信息 ---- */

/*
 * 获取已加载内核模块数量
 */
int funsos_driver_count_modules(void);

/*
 * 获取内核版本字符串
 */
const char *funsos_driver_kernel_version(void);

/*
 * 获取驱动名称（通过设备类型和索引）
 * 参数: dev_type - FUNSOS_DEV_*; index - 设备索引; buf - 接收名称; len - 缓冲区长度
 * 返回: 0 成功, -1 失败
 */
int funsos_driver_get_name(int dev_type, uint32_t index, char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FUNSOS_DRIVER_H */
