/* ssl_tls.h - TLS 1.2/1.3 最小实现 (握手层) */
#ifndef SSL_TLS_H
#define SSL_TLS_H

#include "stdint.h"
#include "stddef.h"

/* TLS 版本 */
#define TLS_VERSION_1_0   0x0301
#define TLS_VERSION_1_1   0x0302
#define TLS_VERSION_1_2   0x0303
#define TLS_VERSION_1_3   0x0304

/* TLS 记录类型 */
#define TLS_RECORD_HANDSHAKE  0x16
#define TLS_RECORD_ALERT      0x15
#define TLS_RECORD_CIPHERTEXT 0x17
#define TLS_RECORD_CHANGE_CIPHER 0x14

/* TLS 握手类型 */
#define HS_CLIENT_HELLO      0x01
#define HS_SERVER_HELLO      0x02
#define HS_CERTIFICATE       0x0B
#define HS_SERVER_KEY_EXCH   0x0C
#define HS_SERVER_HELLO_DONE 0x0E
#define HS_CLIENT_KEY_EXCH   0x10
#define HS_FINISHED          0x14

/* 密码套件 */
#define TLS_RSA_WITH_AES_128_CBC_SHA      0x002F
#define TLS_RSA_WITH_AES_256_CBC_SHA      0x0035
#define TLS_AES_128_GCM_SHA256            0x009C
#define TLS_AES_256_GCM_SHA384            0x009D
#define TLS_CHACHA20_POLY1305_SHA256      0xCCA8

/* TLS 连接状态 */
typedef enum {
    TLS_STATE_CLOSED,
    TLS_STATE_CONNECTING,
    TLS_STATE_HANDSHAKING,
    TLS_STATE_CONNECTED,
    TLS_STATE_ERROR
} tls_state_t;

/* TLS 上下文 */
typedef struct tls_ctx {
    tls_state_t state;
    uint16_t    version;              /* 协商版本 */
    uint16_t    cipher_suite;         /* 选中的密码套件 */

    /* 随机数 */
    uint8_t     client_random[32];    /* ClientHello随机数 */
    uint8_t     server_random[32];    /* ServerHello随机数 */
    uint8_t     master_secret[48];    /* 主密钥 */

    /* 会话 */
    uint32_t    session_id_len;
    uint8_t     session_id[32];

    /* 密钥材料 (简化) */
    uint8_t     client_write_key[32];
    uint8_t     server_write_key[32];
    uint8_t     client_iv[16];
    uint8_t     server_iv[16];

    /* 序列号 */
    uint64_t    client_seq;
    uint64_t    server_seq;

    /* 回调: 底层socket读写 */
    int (*tls_send)(struct tls_ctx *ctx, const void *data, uint32_t len);
    int (*tls_recv)(struct tls_ctx *ctx, void *buf, uint32_t len);
    void *user_data;

    /* 错误码 */
    int         error_code;
    char        error_msg[128];
} tls_ctx_t;

/* 创建TLS上下文 */
tls_ctx_t *tls_create(void);

/* 释放TLS上下文 */
void tls_destroy(tls_ctx_t *ctx);

/* 设置底层传输回调 */
void tls_set_io_callbacks(tls_ctx_t *ctx,
    int (*send_fn)(tls_ctx_t*, const void*, uint32_t),
    int (*recv_fn)(tls_ctx_t*, void*, uint32_t));

/* 开始TLS握手 (客户端模式) */
int  tls_connect(tls_ctx_t *ctx);

/* 在已连接的TLS上发送数据 */
int  tls_write(tls_ctx_t *ctx, const void *data, uint32_t len);

/* 从TLS连接读取数据 */
int  tls_read(tls_ctx_t *ctx, void *buf, uint32_t len);

/* 关闭TLS连接 */
int  tls_close(tls_ctx_t *ctx);

/* 获取当前状态 */
const char *tls_state_str(tls_state_t state);

/* 获取错误信息 */
const char *tls_error_str(const tls_ctx_t *ctx);

#endif /* SSL_TLS_H */
