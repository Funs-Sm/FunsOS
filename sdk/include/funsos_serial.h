#ifndef FUNSOS_SERIAL_H
#define FUNSOS_SERIAL_H

/*
 * FUNSOS 串口通信 API
 * 提供串口打开、配置、读写等功能。
 * 基于 kernel/drivers/serial.c 和 apps/user_syscall.h 的系统调用。
 */

#include "stdint.h"

/* 串口波特率 */
#define FUNSOS_B0         0
#define FUNSOS_B50        1
#define FUNSOS_B75        2
#define FUNSOS_B110       3
#define FUNSOS_B134       4
#define FUNSOS_B150       5
#define FUNSOS_B200       6
#define FUNSOS_B300       7
#define FUNSOS_B600       8
#define FUNSOS_B1200      9
#define FUNSOS_B1800      10
#define FUNSOS_B2400      11
#define FUNSOS_B4800      12
#define FUNSOS_B9600      13
#define FUNSOS_B19200     14
#define FUNSOS_B38400     15
#define FUNSOS_B57600     16
#define FUNSOS_B115200    17
#define FUNSOS_B230400    18
#define FUNSOS_B460800    19
#define FUNSOS_B500000    20
#define FUNSOS_B576000    21
#define FUNSOS_B921600    22
#define FUNSOS_B1000000   23
#define FUNSOS_B1152000   24
#define FUNSOS_B1500000   25
#define FUNSOS_B2000000   26
#define FUNSOS_B2500000   27
#define FUNSOS_B3000000   28
#define FUNSOS_B3500000   29
#define FUNSOS_B4000000   30

/* 数据位 */
#define FUNSOS_CS5        0
#define FUNSOS_CS6        1
#define FUNSOS_CS7        2
#define FUNSOS_CS8        3

/* 停止位 */
#define FUNSOS_STOPBITS_1  0    /* 1个停止位 */
#define FUNSOS_STOPBITS_2  1    /* 2个停止位 */

/* 校验位 */
#define FUNSOS_PARITY_NONE  0   /* 无校验 */
#define FUNSOS_PARITY_ODD   1   /* 奇校验 */
#define FUNSOS_PARITY_EVEN  2   /* 偶校验 */
#define FUNSOS_PARITY_MARK  3   /* 标记校验 */
#define FUNSOS_PARITY_SPACE 4   /* 空校验 */

/* 流控制 */
#define FUNSOS_FLOW_NONE    0   /* 无流控制 */
#define FUNSOS_FLOW_HARD    1   /* 硬件流控制 (RTS/CTS) */
#define FUNSOS_FLOW_SOFT    2   /* 软件流控制 (XON/XOFF) */

/* 串口配置结构 */
typedef struct {
    uint32_t baud_rate;     /* 波特率 (FUNSOS_Bxxxx) */
    uint8_t  data_bits;     /* 数据位 (CS5-CS8) */
    uint8_t  stop_bits;     /* 停止位 */
    uint8_t  parity;        /* 校验位 */
    uint8_t  flow_control;  /* 流控制 */
    int      xon_xoff;      /* 是否启用软件流控 */
    int      rts_cts;       /* 是否启用硬件流控 */
} funsos_serial_config_t;

/* 串口引脚状态 */
#define FUNSOS_SERIAL_DTR   0x001   /* 数据终端就绪 */
#define FUNSOS_SERIAL_RTS   0x002   /* 请求发送 */
#define FUNSOS_SERIAL_CTS   0x010   /* 清除发送 */
#define FUNSOS_SERIAL_DSR   0x020   /* 数据装置就绪 */
#define FUNSOS_SERIAL_RI    0x040   /* 振铃指示 */
#define FUNSOS_SERIAL_DCD   0x080   /* 数据载波检测 */

/* ---- 打开/关闭 ---- */

/*
 * 打开串口
 * 参数: port - 串口设备路径 (如 "/dev/ttyS0")
 * 返回: 文件描述符, -1 失败
 */
int funsos_serial_open(const char *port);

/*
 * 关闭串口
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_close(int fd);

/* ---- 配置 ---- */

/*
 * 配置串口
 * 参数: fd - 文件描述符; config - 配置结构
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_config(int fd, const funsos_serial_config_t *config);

/*
 * 获取串口配置
 * 参数: fd - 文件描述符; config - 接收配置的结构
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_get_config(int fd, funsos_serial_config_t *config);

/*
 * 设置波特率
 * 参数: fd - 文件描述符; baud - 波特率
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_baud(int fd, uint32_t baud);

/*
 * 设置数据位
 * 参数: fd - 文件描述符; bits - 数据位 (CS5-CS8)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_databits(int fd, uint8_t bits);

/*
 * 设置停止位
 * 参数: fd - 文件描述符; bits - 停止位 (1或2)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_stopbits(int fd, uint8_t bits);

/*
 * 设置校验位
 * 参数: fd - 文件描述符; parity - 校验方式
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_parity(int fd, uint8_t parity);

/* ---- 读写 ---- */

/*
 * 从串口读取数据
 * 参数: fd - 文件描述符; buf - 接收缓冲区; count - 读取字节数
 * 返回: 实际读取的字节数, -1 失败
 */
int funsos_serial_read(int fd, void *buf, uint32_t count);

/*
 * 向串口写入数据
 * 参数: fd - 文件描述符; buf - 写入数据; count - 写入字节数
 * 返回: 实际写入的字节数, -1 失败
 */
int funsos_serial_write(int fd, const void *buf, uint32_t count);

/*
 * 设置读取超时
 * 参数: fd - 文件描述符; timeout_ms - 超时时间(毫秒), -1=无限
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_read_timeout(int fd, int timeout_ms);

/*
 * 设置写入超时
 * 参数: fd - 文件描述符; timeout_ms - 超时时间(毫秒), -1=无限
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_write_timeout(int fd, int timeout_ms);

/* ---- 控制 ---- */

/*
 * 刷新缓冲区
 * 参数: fd - 文件描述符; queue - 刷新队列选择 (0=输入, 1=输出, 2=全部)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_flush(int fd, int queue);

/*
 * 获取可用字节数
 * 参数: fd - 文件描述符
 * 返回: 可用字节数, -1 失败
 */
int funsos_serial_available(int fd);

/*
 * 设置 DTR 引脚状态
 * 参数: fd - 文件描述符; level - 电平 (1=高, 0=低)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_dtr(int fd, int level);

/*
 * 设置 RTS 引脚状态
 * 参数: fd - 文件描述符; level - 电平 (1=高, 0=低)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_set_rts(int fd, int level);

/*
 * 获取调制解调器引脚状态
 * 参数: fd - 文件描述符; status - 接收状态的指针
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_get_modem_status(int fd, uint32_t *status);

/*
 * 发送中断信号
 * 参数: fd - 文件描述符; duration_ms - 持续时间(毫秒)
 * 返回: 0 成功, -1 失败
 */
int funsos_serial_break(int fd, uint32_t duration_ms);

#endif /* FUNSOS_SERIAL_H */
