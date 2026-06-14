#ifndef HW_RNG_H
#define HW_RNG_H

/*
 * hw_rng.h - 硬件随机数生成器驱动接口
 *
 * 提供对硬件RNG的统一访问接口，用于密码学安全随机数。
 *
 * 支持的硬件源:
 *   - Intel Secure Key RNG (RDRAND 指令, Ivy Bridge+)
 *   - Intel RDSEED 指令 (Haswell+, 更强)
 *   - AMD RNG (PQ8000/8B00 端口)
 *   - VIA PadLock RNG
 *   - 软件回退: 基于定时器抖动的伪随机
 */

#include <stdint.h>

/* ---- 初始化 ---- */
int  hw_rng_init(void);              /* 探测并初始化可用的硬件RNG */
int  hw_rng_available(void);         /* 检查是否有可用硬件RNG */
const char *hw_rng_name(void);       /* 返回当前使用的RNG名称 */

/* ---- 基本操作 ---- */
/*
 * hw_rng_read() - 从硬件读取随机字节
 *   buf: 输出缓冲区
 *   len: 要读取的字节数
 *   返回: 实际成功读取的字节数(可能 < len 如果硬件暂时不可用)
 */
uint32_t hw_rng_read(uint8_t *buf, uint32_t len);

/* ---- 便捷函数 ---- */
uint32_t hw_rng_u32(void);           /* 生成一个32位随机整数 */
uint64_t hw_rng_u64(void);           /* 生成一个64位随机整数 */
uint8_t  hw_rng_byte(void);          /* 生成一个随机字节 */

/* ---- 熵质量 ---- */
typedef enum {
    RNG_ENTROPY_LOW = 0,     /* 软件模拟, 低质量 */
    RNG_ENTROPY_MEDIUM = 1,  /* 定时器抖动, 中等质量 */
    RNG_ENTROPY_HIGH = 2,    /* 硬件RNG, 高质量(CSPRNG级) */
    RNG_ENTROPY_SECURE = 3   /* 经过认证的安全级别 */
} rng_entropy_level_t;

rng_entropy_level_t hw_rng_get_quality(void);

#endif /* HW_RNG_H */
