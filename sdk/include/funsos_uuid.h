#ifndef FUNSOS_UUID_H
#define FUNSOS_UUID_H

/*
 * FUNSOS UUID 生成 API
 * 提供 UUID 生成、解析、比较等功能。
 * 基于 kernel/crypto.h 和 kernel/krng.h 的随机数生成器。
 */

#include "stdint.h"

/* UUID 长度 */
#define FUNSOS_UUID_LEN        16    /* UUID 二进制长度 (16字节) */
#define FUNSOS_UUID_STR_LEN    36    /* UUID 字符串长度 (36字符, 不含\0) */

/* UUID 版本 */
#define FUNSOS_UUID_VERSION_1  1   /* 基于时间和MAC地址 */
#define FUNSOS_UUID_VERSION_3  3   /* 基于名字空间 (MD5) */
#define FUNSOS_UUID_VERSION_4  4   /* 随机生成 */
#define FUNSOS_UUID_VERSION_5  5   /* 基于名字空间 (SHA1) */

/* UUID 结构 (16字节) */
typedef struct {
    uint8_t bytes[FUNSOS_UUID_LEN];
} funsos_uuid_t;

/* 常用 UUID 名字空间 */
extern const funsos_uuid_t FUNSOS_UUID_NAMESPACE_DNS;
extern const funsos_uuid_t FUNSOS_UUID_NAMESPACE_URL;
extern const funsos_uuid_t FUNSOS_UUID_NAMESPACE_OID;
extern const funsos_uuid_t FUNSOS_UUID_NAMESPACE_X500;

/*
 * 生成随机 UUID (版本4)
 * 参数: uuid - 接收生成的 UUID
 * 返回: 0 成功, -1 失败
 */
int funsos_uuid_generate(funsos_uuid_t *uuid);

/*
 * 生成随机 UUID 字符串 (版本4)
 * 参数: buf - 接收字符串的缓冲区 (至少 37 字节)
 * 返回: 字符串指针, NULL 失败
 */
char *funsos_uuid_generate_string(char *buf);

/*
 * 基于名字空间生成 UUID (版本3/5)
 * 参数: ns - 名字空间 UUID; name - 名称; namelen - 名称长度
 *       uuid - 接收生成的 UUID; version - 版本 (3或5)
 * 返回: 0 成功, -1 失败
 */
int funsos_uuid_generate_name(const funsos_uuid_t *ns, const char *name,
                               uint32_t namelen, funsos_uuid_t *uuid, int version);

/*
 * 解析 UUID 字符串为二进制
 * 参数: str - UUID 字符串; uuid - 接收二进制 UUID
 * 返回: 0 成功, -1 失败
 */
int funsos_uuid_parse(const char *str, funsos_uuid_t *uuid);

/*
 * 将二进制 UUID 格式化为字符串
 * 参数: uuid - 二进制 UUID; buf - 接收字符串的缓冲区 (至少 37 字节)
 * 返回: 字符串指针
 */
char *funsos_uuid_unparse(const funsos_uuid_t *uuid, char *buf);

/*
 * 比较两个 UUID
 * 参数: u1 - 第一个 UUID; u2 - 第二个 UUID
 * 返回: 0 相等, <0 u1<u2, >0 u1>u2
 */
int funsos_uuid_compare(const funsos_uuid_t *u1, const funsos_uuid_t *u2);

/*
 * 检查 UUID 是否为空 (全零)
 * 参数: uuid - UUID
 * 返回: 1 空, 0 非空
 */
int funsos_uuid_is_null(const funsos_uuid_t *uuid);

/*
 * 清空 UUID (设为全零)
 * 参数: uuid - UUID
 */
void funsos_uuid_clear(funsos_uuid_t *uuid);

/*
 * 获取 UUID 版本
 * 参数: uuid - UUID
 * 返回: 版本号 (1-5)
 */
int funsos_uuid_version(const funsos_uuid_t *uuid);

/*
 * 复制 UUID
 * 参数: dst - 目标; src - 源
 */
void funsos_uuid_copy(funsos_uuid_t *dst, const funsos_uuid_t *src);

#endif /* FUNSOS_UUID_H */
