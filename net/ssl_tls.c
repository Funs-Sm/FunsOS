/* ssl_tls.c - TLS 1.2/1.3 最小实现 (握手层)
 *
 * 提供简化的TLS握手流程, 结构上符合标准但加密操作被简化.
 * 主要用途:
 *   - 教学演示TLS协议结构
 *   - 为FunsOS提供TLS框架基础
 *   - 支持基本的HTTPS连接建立
 *
 * 注意: 此实现不提供真正的密码学安全性!
 * 生产环境请使用成熟的TLS库 (如mbedTLS, OpenSSL).
 */

#include "ssl_tls.h"
#include "string.h"
#include "stdlib.h"

/* ========== 内部常量定义 ========== */

/* TLS记录层头部大小 */
#define TLS_RECORD_HEADER_SIZE 5
/* 类型(1) + 版本(2) + 长度(2) */

/* 握手消息头部大小 */
#define TLS_HANDSHAKE_HEADER_SIZE 4
/* 类型(1) + 长度(3) */

/* 支持的密码套件列表 */
static const uint16_t supported_suites[] = {
    TLS_AES_128_GCM_SHA256,
    TLS_AES_256_GCM_SHA384,
    TLS_CHACHA20_POLY1305_SHA256,
    TLS_RSA_WITH_AES_128_CBC_SHA,
    TLS_RSA_WITH_AES_256_CBC_SHA
};
#define SUPPORTED_SUITES_COUNT (sizeof(supported_suites) / sizeof(supported_suites[0]))

/* 默认使用的密码套件 (优先选择AES-128-GCM) */
#define DEFAULT_CIPHER_SUITE TLS_AES_128_GCM_SHA256

/* TLS记录层最大负载 */
#define TLS_MAX_PLAINTEXT_RECORD 16384
#define TLS_MAX_CIPHERTEXT_RECORD 16640  /* 明文 + tag(16) + padding */

/* ========== 内部辅助函数 ========== */

/*
 * 构建TLS记录层帧
 * 格式: [类型(1)] [版本(2)] [长度(2)] [数据...]
 */
static int build_record(uint8_t *buf, uint32_t buf_size,
                        uint8_t record_type, uint16_t version,
                        const uint8_t *data, uint32_t data_len)
{
    if (TLS_RECORD_HEADER_SIZE + data_len > buf_size)
        return -1;

    uint32_t off = 0;

    /* 记录类型 */
    buf[off++] = record_type;

    /* 协议版本 */
    buf[off++] = (version >> 8) & 0xFF;
    buf[off++] = version & 0xFF;

    /* 长度 (大端序) */
    buf[off++] = (data_len >> 8) & 0xFF;
    buf[off++] = data_len & 0xFF;

    /* 数据载荷 */
    if (data && data_len > 0) {
        memcpy(buf + off, data, data_len);
        off += data_len;
    }

    return (int)off;
}

/*
 * 构建握手消息
 * 格式: [类型(1)] [长度(3)] [数据...]
 */
static int build_handshake(uint8_t *buf, uint32_t buf_size,
                           uint8_t hs_type, const uint8_t *data, uint32_t data_len)
{
    if (TLS_HANDSHAKE_HEADER_SIZE + data_len > buf_size)
        return -1;

    uint32_t off = 0;

    /* 握手类型 */
    buf[off++] = hs_type;

    /* 长度 (24位, 大端序) */
    buf[off++] = (data_len >> 16) & 0xFF;
    buf[off++] = (data_len >> 8) & 0xFF;
    buf[off++] = data_len & 0xFF;

    /* 握手数据 */
    if (data && data_len > 0) {
        memcpy(buf + off, data, data_len);
        off += data_len;
    }

    return (int)off;
}

/*
 * 生成伪随机数 (简化版)
 * 在实际系统中应使用硬件RNG或CSPRNG
 */
static void generate_random(uint8_t *buf, uint32_t len)
{
    static uint32_t seed = 12345;
    uint32_t i;
    for (i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (seed >> 16) & 0xFF;
    }
}

/*
 * 发送TLS记录
 */
static int send_record(tls_ctx_t *ctx, uint8_t type, const uint8_t *data, uint32_t len)
{
    uint8_t record[TLS_RECORD_HEADER_SIZE + TLS_MAX_PLAINTEXT_RECORD];
    int rec_len;

    if (ctx->state >= TLS_STATE_CONNECTED) {
        /*
         * 已完成握手: 数据需要加密
         * 简化实现: 直接发送明文 (不安全!)
         * 实际应该使用协商的密钥进行加密
         */
        rec_len = build_record(record, sizeof(record), type,
                               ctx->version, data, len);
    } else {
        /* 握手阶段: 明文发送 */
        rec_len = build_record(record, sizeof(record), type,
                               ctx->version, data, len);
    }

    if (rec_len < 0)
        return -1;

    /* 通过回调发送 */
    if (ctx->tls_send) {
        return ctx->tls_send(ctx, record, (uint32_t)rec_len);
    }
    return -1;
}

/*
 * 接收并解析TLS记录
 * 返回: 接收到的字节数, 或负数表示错误
 */
static int recv_record(tls_ctx_t *ctx, uint8_t *out_type,
                       uint8_t *buf, uint32_t buf_size)
{
    uint8_t header[TLS_RECORD_HEADER_SIZE];
    int n;

    /* 读取记录头 */
    n = ctx->tls_recv(ctx, header, TLS_RECORD_HEADER_SIZE);
    if (n != TLS_RECORD_HEADER_SIZE)
        return -1;

    /* 解析头部 */
    *out_type = header[0];
    uint16_t version = ((uint16_t)header[1] << 8) | header[2];
    uint16_t length = ((uint16_t)header[3] << 8) | header[4];

    /* 验证长度 */
    if (length > buf_size || length == 0)
        return -2;

    /* 读取记录数据 */
    n = ctx->tls_recv(ctx, buf, length);
    if (n != (int)length)
        return -3;

    return n;
}

/*
 * 从记录中提取握手消息
 */
static int parse_handshake(const uint8_t *record_data, uint32_t record_len,
                           uint8_t *hs_type, uint8_t **hs_data, uint32_t *hs_len)
{
    if (record_len < TLS_HANDSHAKE_HEADER_SIZE)
        return -1;

    *hs_type = record_data[0];
    *hs_len = ((uint32_t)record_data[1] << 16) |
              ((uint32_t)record_data[2] << 8) |
               (uint32_t)record_data[3];

    if (*hs_len + TLS_HANDSHAKE_HEADER_SIZE > record_len)
        return -2;

    *hs_data = (uint8_t *)(record_data + TLS_HANDSHAKE_HEADER_SIZE);
    return 0;
}

/*
 * 构建ClientHello消息 (简化版)
 *
 * ClientHello结构:
 *   client_version (2)
 *   random (32)
 *   session_id_len (1) + session_id (variable)
 *   cipher_suites_len (2) + cipher_suites (variable)
 *   compression_methods_len (1) + compression_methods (variable)
 *   extensions (可选)
 */
static int build_client_hello(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    uint8_t hello[512];  /* ClientHello通常不超过512字节 */
    uint32_t off = 0;

    /* 客户端支持的最高版本 (尝试TLS 1.2) */
    hello[off++] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    hello[off++] = TLS_VERSION_1_2 & 0xFF;

    /* 客户端随机数 (32字节) */
    generate_random(ctx->client_random, 32);
    memcpy(hello + off, ctx->client_random, 32);
    off += 32;

    /* 会话ID (空, 表示新会话) */
    hello[off++] = 0;  /* session_id长度 = 0 */

    /* 密码套件列表 */
    uint16_t suites_len = SUPPORTED_SUITES_COUNT * 2;  /* 每个套件2字节 */
    hello[off++] = (suites_len >> 8) & 0xFF;
    hello[off++] = suites_len & 0xFF;

    int i;
    for (i = 0; i < SUPPORTED_SUITES_COUNT; i++) {
        hello[off++] = (supported_suites[i] >> 8) & 0xFF;
        hello[off++] = supported_suites[i] & 0xFF;
    }

    /* 压缩方法 (只支持null压缩) */
    hello[off++] = 1;  /* 方法数量 */
    hello[off++] = 0;  /* null compression */

    /* 扩展 (简化: 不发送扩展) */
    /* 在完整实现中应包含SNI、ALPN、Supported Groups等扩展 */

    /* 包装成握手消息 */
    uint8_t hs_buf[TLS_HANDSHAKE_HEADER_SIZE + 512];
    int hs_len = build_handshake(hs_buf, sizeof(hs_buf),
                                 HS_CLIENT_HELLO, hello, off);
    if (hs_len < 0)
        return -1;

    /* 包装成记录 */
    return build_record(buf, buf_size, TLS_RECORD_HANDSHAKE,
                        TLS_VERSION_1_2, hs_buf, (uint32_t)hs_len);
}

/*
 * 解析ServerHello消息
 */
static int parse_server_hello(tls_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;

    if (len < 38)  /* 最小长度: version(2)+random(32)+session_id_len(1)+... */
        return -1;

    /* 服务器选择的版本 */
    ctx->version = ((uint16_t)data[off] << 8) | data[off + 1];
    off += 2;

    /* 服务器随机数 */
    memcpy(ctx->server_random, data + off, 32);
    off += 32;

    /* 会话ID */
    uint8_t sid_len = data[off++];
    if (sid_len > 32 || off + sid_len > len)
        return -2;

    ctx->session_id_len = sid_len;
    if (sid_len > 0) {
        memcpy(ctx->session_id, data + off, sid_len);
        off += sid_len;
    }

    /* 密码套件 */
    if (off + 2 > len)
        return -3;
    ctx->cipher_suite = ((uint16_t)data[off] << 8) | data[off + 1];
    off += 2;

    /* 压缩方法 */
    if (off + 1 > len)
        return -4;
    /* uint8_t compression = data[off++]; */  /* 忽略压缩方法 */

    /* 验证密码套件是否支持 */
    int supported = 0;
    for (int i = 0; i < SUPPORTED_SUITES_COUNT; i++) {
        if (ctx->cipher_suite == supported_suites[i]) {
            supported = 1;
            break;
        }
    }

    if (!supported) {
        strcpy(ctx->error_msg, "Unsupported cipher suite");
        ctx->error_code = -10;
        return -5;
    }

    return 0;
}

/*
 * 简化的主密钥生成 (PRF stub)
 * 实际应使用HKDF或TLS PRF
 */
static void compute_master_secret_stub(tls_ctx_t *ctx)
{
    /*
     * 真实的master_secret计算:
     * master_secret = PRF(pre_master_secret,
     *                     "master secret",
     *                     ClientHello.random + ServerHello.random)
     *                    [0..47]
     *
     * 这里使用简化版本: 直接拼接哈希
     */
    uint32_t i;
    for (i = 0; i < 48; i++) {
        /* 使用预主密钥和随机数混合 */
        ctx->master_secret[i] = ctx->client_random[i % 32] ^
                               ctx->server_random[i % 32] ^
                               (uint8_t)(i * 17 + 42);  /* 固定盐值 */
    }
}

/*
 * 简化的密钥材料派生 (key expansion stub)
 */
static void derive_keys_stub(tls_ctx_t *ctx)
{
    /*
     * 实际的密钥派生:
     * key_block = PRF(master_secret,
     *                 "key expansion",
     *                 ServerHello.random + ClientHello.random)
     *
     * key_block分解为:
     *   client_write_MAC_key[security_params.mac_key_length]
     *   server_write_MAC_key[security_params.mac_key_length]
     *   client_write_key[security_params.enc_key_length]
     *   server_write_key[security_params.enc_key_length]
     *   client_write_IV[security_params.fixed_iv_length]
     *   server_write_IV[security_params.fixed_iv_length]
     */

    /* 简化: 用master_secret的不同部分作为各密钥 */
    memset(ctx->client_write_key, 0, sizeof(ctx->client_write_key));
    memset(ctx->server_write_key, 0, sizeof(ctx->server_write_key));
    memset(ctx->client_iv, 0, sizeof(ctx->client_iv));
    memset(ctx->server_iv, 0, sizeof(ctx->server_iv));

    /* 从master_secret复制部分字节作为密钥 (仅示意) */
    uint32_t key_len = 16;  /* AES-128 */
    if (ctx->cipher_suite == TLS_AES_256_GCM_SHA384 ||
        ctx->cipher_suite == TLS_RSA_WITH_AES_256_CBC_SHA) {
        key_len = 32;  /* AES-256 */
    }

    memcpy(ctx->client_write_key, ctx->master_secret, key_len);
    memcpy(ctx->server_write_key, ctx->master_secret + 48 - key_len, key_len);
    memcpy(ctx->client_iv, ctx->master_secret + 16, 12);  /* GCM IV通常是12字节 */
    memcpy(ctx->server_iv, ctx->master_secret + 28, 12);
}

/*
 * 构建ClientKeyExchange消息 (RSA模式简化版)
 */
static int build_client_key_exchange(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    /*
     * RSA密钥交换:
     * 客户端生成pre_master_secret (46字节: version(2) + random(44))
     * 用服务器证书的公钥加密后发送
     *
     * 简化实现: 发送固定的pre_master_secret (不加密!)
     */
    uint8_t pms[48];

    /* pre_master_secret的前两字节是客户端建议的版本 */
    pms[0] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    pms[1] = TLS_VERSION_1_2 & 0xFF;
    generate_random(pms + 2, 46);  /* 其余44字节随机 */

    /* 计算主密钥 */
    compute_master_secret_stub(ctx);

    /* 加密后的PMS长度 (RSA加密后的长度, 这里用原始长度代替) */
    uint8_t cke[64];  /* encrypted_pms_length(2) + encrypted_pre_master_secret */
    uint16_t enc_len = 48;  /* 简化: 未加密的长度 */
    cke[0] = (enc_len >> 8) & 0xFF;
    cke[1] = enc_len & 0xFF;
    memcpy(cke + 2, pms, 48);

    /* 包装成握手消息 */
    uint8_t hs_buf[TLS_HANDSHAKE_HEADER_SIZE + 64];
    int hs_len = build_handshake(hs_buf, sizeof(hs_buf),
                                 HS_CLIENT_KEY_EXCH, cke, 50);
    if (hs_len < 0)
        return -1;

    return build_record(buf, buf_size, TLS_RECORD_HANDSHAKE,
                        ctx->version, hs_buf, (uint32_t)hs_len);
}

/*
 * 构建ChangeCipherSpec消息
 */
static int build_change_cipher_spec(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    uint8_t ccs = 1;  /* ChangeCipherSpec消息体固定为1 */
    return build_record(buf, buf_size, TLS_RECORD_CHANGE_CIPHER,
                        ctx->version, &ccs, 1);
}

/*
 * 构建Finished消息 (verify_data = PRF(master_secret, finished_label, handshake_hash)) */
static int build_finished(tls_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    /*
     * Finished消息包含12字节的verify_data
     * verify_data = PRF(master_secret, finished_label, Hash(handshake_messages))
     *
     * 简化: 使用固定模式的验证数据
     */
    uint8_t verify_data[12];
    uint32_t i;
    for (i = 0; i < 12; i++) {
        verify_data[i] = (uint8_t)(ctx->client_seq + i) ^ 0xAB;
    }

    /* 包装成握手消息 */
    uint8_t hs_buf[TLS_HANDSHAKE_HEADER_SIZE + 12];
    int hs_len = build_handshake(hs_buf, sizeof(hs_buf),
                                 HS_FINISHED, verify_data, 12);
    if (hs_len < 0)
        return -1;

    return build_record(buf, buf_size, TLS_RECORD_HANDSHAKE,
                        ctx->version, hs_buf, (uint32_t)hs_len);
}

/* ========== 公共API实现 ========== */

/*
 * tls_create - 创建新的TLS上下文
 */
tls_ctx_t *tls_create(void)
{
    tls_ctx_t *ctx = (tls_ctx_t *)malloc(sizeof(tls_ctx_t));
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(tls_ctx_t));
    ctx->state = TLS_STATE_CLOSED;
    ctx->error_code = 0;
    ctx->error_msg[0] = '\0';

    return ctx;
}

/*
 * tls_destroy - 销毁TLS上下文并释放资源
 */
void tls_destroy(tls_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* 安全清除敏感数据 */
    memset(ctx->client_random, 0, sizeof(ctx->client_random));
    memset(ctx->server_random, 0, sizeof(ctx->server_random));
    memset(ctx->master_secret, 0, sizeof(ctx->master_secret));
    memset(ctx->client_write_key, 0, sizeof(ctx->client_write_key));
    memset(ctx->server_write_key, 0, sizeof(ctx->server_write_key));

    free(ctx);
}

/*
 * tls_set_io_callbacks - 设置底层I/O回调函数
 */
void tls_set_io_callbacks(tls_ctx_t *ctx,
                          int (*send_fn)(tls_ctx_t*, const void*, uint32_t),
                          int (*recv_fn)(tls_ctx_t*, void*, uint32_t))
{
    if (!ctx)
        return;

    ctx->tls_send = send_fn;
    ctx->tls_recv = recv_fn;
}

/*
 * tls_connect - 执行TLS客户端握手
 *
 * 完整的TLS 1.2握手流程:
 * 1. Client --> Server: ClientHello
 * 2. Server --> Client: ServerHello
 *                      Certificate
 *                      ServerKeyExchange (可选)
 *                      ServerHelloDone
 * 3. Client --> Server: ClientKeyExchange
 *                      ChangeCipherSpec
 *                      Finished (encrypted)
 * 4. Server --> Client: ChangeCipherSpec
 *                      Finished (encrypted)
 */
int tls_connect(tls_ctx_t *ctx)
{
    if (!ctx || !ctx->tls_send || !ctx->tls_recv) {
        if (ctx) {
            ctx->state = TLS_STATE_ERROR;
            strcpy(ctx->error_msg, "Invalid context or missing callbacks");
            ctx->error_code = -1;
        }
        return -1;
    }

    ctx->state = TLS_STATE_CONNECTING;

    /* ====== 第一步: 发送ClientHello ====== */
    uint8_t client_hello_buf[1024];
    int ch_len = build_client_hello(ctx, client_hello_buf, sizeof(client_hello_buf));
    if (ch_len < 0) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to build ClientHello");
        ctx->error_code = -2;
        return -2;
    }

    int sent = ctx->tls_send(ctx, client_hello_buf, (uint32_t)ch_len);
    if (sent != ch_len) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to send ClientHello");
        ctx->error_code = -3;
        return -3;
    }

    ctx->state = TLS_STATE_HANDSHAKING;

    /* ====== 第二步: 接收ServerHello ====== */
    uint8_t recv_type;
    uint8_t recv_buf[TLS_MAX_CIPHERTEXT_RECORD];
    int recv_len;

    recv_len = recv_record(ctx, &recv_type, recv_buf, sizeof(recv_buf));
    if (recv_len <= 0) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to receive ServerHello");
        ctx->error_code = -4;
        return -4;
    }

    if (recv_type != TLS_RECORD_HANDSHAKE) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Expected handshake record");
        ctx->error_code = -5;
        return -5;
    }

    /* 解析ServerHello握手消息 */
    uint8_t *hs_data;
    uint32_t hs_len;
    uint8_t hs_type;

    if (parse_handshake(recv_buf, (uint32_t)recv_len, &hs_type, &hs_data, &hs_len) != 0) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to parse ServerHello");
        ctx->error_code = -6;
        return -6;
    }

    if (hs_type != HS_SERVER_HELLO) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Expected ServerHello message");
        ctx->error_code = -7;
        return -7;
    }

    if (parse_server_hello(ctx, hs_data, hs_len) != 0) {
        ctx->state = TLS_STATE_ERROR;
        /* error_msg已在parse_server_hello中设置 */
        return ctx->error_code;
    }

    /*
     * 接下来应该接收Certificate, ServerKeyExchange, ServerHelloDone
     * 简化实现: 假设这些消息都在同一个记录中或可以跳过
     * 完整实现需要逐个接收和处理每条消息
     */

    /* 尝试接收剩余的服务器握手消息 (简化: 循环直到收到ServerHelloDone) */
    int got_server_done = 0;
    int max_iterations = 10;  /* 防止无限循环 */

    while (!got_server_done && max_iterations-- > 0) {
        recv_len = recv_record(ctx, &recv_type, recv_buf, sizeof(recv_buf));
        if (recv_len <= 0)
            break;

        if (recv_type != TLS_RECORD_HANDSHAKE)
            continue;

        /* 可能在一个记录中有多个握手消息 */
        uint32_t pos = 0;
        while (pos + TLS_HANDSHAKE_HEADER_SIZE <= (uint32_t)recv_len) {
            if (parse_handshake(recv_buf + pos, (uint32_t)recv_len - pos,
                               &hs_type, &hs_data, &hs_len) != 0)
                break;

            switch (hs_type) {
            case HS_CERTIFICATE:
                /* 应该解析并验证服务器证书 */
                /* 简化: 跳过证书验证 */
                break;

            case HS_SERVER_KEY_EXCH:
                /* 应该提取DH参数或RSA公钥 */
                /* 简化: 跳过 */
                break;

            case HS_SERVER_HELLO_DONE:
                got_server_done = 1;
                break;

            default:
                /* 忽略未知消息 */
                break;
            }

            pos += TLS_HANDSHAKE_HEADER_SIZE + hs_len;
        }
    }

    if (!got_server_done) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Did not receive ServerHelloDone");
        ctx->error_code = -8;
        return -8;
    }

    /* ====== 第三步: 发送ClientKeyExchange + ChangeCipherSpec + Finished ====== */

    /* 发送ClientKeyExchange */
    uint8_t cke_buf[256];
    int cke_len = build_client_key_exchange(ctx, cke_buf, sizeof(cke_buf));
    if (cke_len < 0) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to build ClientKeyExchange");
        ctx->error_code = -9;
        return -9;
    }

    sent = ctx->tls_send(ctx, cke_buf, (uint32_t)cke_len);
    if (sent != cke_len) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Failed to send ClientKeyExchange");
        ctx->error_code = -10;
        return -10;
    }

    /* 派生密钥材料 */
    derive_keys_stub(ctx);

    /* 发送ChangeCipherSpec */
    uint8_t ccs_buf[16];
    int ccs_len = build_change_cipher_spec(ctx, ccs_buf, sizeof(ccs_buf));
    if (ccs_len > 0) {
        ctx->tls_send(ctx, ccs_buf, (uint32_t)ccs_len);
    }

    /* 发送Finished (此时后续消息将被"加密") */
    uint8_t fin_buf[64];
    int fin_len = build_finished(ctx, fin_buf, sizeof(fin_buf));
    if (fin_len > 0) {
        ctx->tls_send(ctx, fin_buf, (uint32_t)fin_len);
    }

    /* ====== 第四步: 接收服务器的ChangeCipherSpec + Finished ====== */
    max_iterations = 5;
    got_server_done = 0;  /* 重用变量表示是否收到服务器Finished */

    while (!got_server_done && max_iterations-- > 0) {
        recv_len = recv_record(ctx, &recv_type, recv_buf, sizeof(recv_buf));
        if (recv_len <= 0)
            break;

        if (recv_type == TLS_RECORD_CHANGE_CIPHER) {
            /* 收到ChangeCipherSpec, 下一条记录应该是加密的Finished */
            continue;
        }

        if (recv_type == TLS_RECORD_HANDSHAKE) {
            if (parse_handshake(recv_buf, (uint32_t)recv_len,
                               &hs_type, &hs_data, &hs_len) == 0 &&
                hs_type == HS_FINISHED) {
                got_server_done = 1;
                /* 应该验证服务器的verify_data */
                /* 简化: 跳过验证 */
            }
        } else if (recv_type == TLS_RECORD_CIPHERTEXT) {
            /* 加密的Finished消息 (TLS 1.3) */
            got_server_done = 1;
        }
    }

    if (!got_server_done) {
        ctx->state = TLS_STATE_ERROR;
        strcpy(ctx->error_msg, "Handshake did not complete");
        ctx->error_code = -11;
        return -11;
    }

    /* 握手成功! */
    ctx->state = TLS_STATE_CONNECTED;
    ctx->client_seq = 0;
    ctx->server_seq = 0;

    return 0;
}

/*
 * tls_write - 在已连接的TLS会话上发送应用数据
 */
int tls_write(tls_ctx_t *ctx, const void *data, uint32_t len)
{
    if (!ctx || ctx->state != TLS_STATE_CONNECTED)
        return -1;

    if (!data || len == 0)
        return 0;

    /*
     * TLS应用数据记录:
     * - 类型: TLS_RECORD_CIPHERTEXT (0x17)
     * - 加密数据 (GCM/AES等)
     *
     * 简化实现: 直接发送明文 (不安全!)
     */
    int result = send_record(ctx, TLS_RECORD_CIPHERTEXT,
                             (const uint8_t *)data, len);

    if (result > 0) {
        ctx->client_seq++;
        return (int)len;  /* 返回原始数据长度 */
    }

    return result;
}

/*
 * tls_read - 从已连接的TLS会话读取应用数据
 */
int tls_read(tls_ctx_t *ctx, void *buf, uint32_t len)
{
    if (!ctx || ctx->state != TLS_STATE_CONNECTED)
        return -1;

    if (!buf || len == 0)
        return 0;

    /*
     * 读取TLS记录并解密
     * 简化实现: 直接返回明文 (不安全!)
     */
    uint8_t recv_type;
    int recv_len = recv_record(ctx, &recv_type, (uint8_t *)buf, len);

    if (recv_len > 0) {
        ctx->server_seq++;
    }

    return recv_len;
}

/*
 * tls_close - 关闭TLS连接
 */
int tls_close(tls_ctx_t *ctx)
{
    if (!ctx)
        return -1;

    if (ctx->state == TLS_STATE_CLOSED)
        return 0;

    /* 发送关闭通知Alert (warning level) */
    if (ctx->state == TLS_STATE_CONNECTED && ctx->tls_send) {
        uint8_t alert[8];  /* Alert记录: level(1) + description(1) */
        alert[0] = 1;  /* warning */
        alert[1] = 0;  /* close_notify */

        uint8_t record[16];
        int rec_len = build_record(record, sizeof(record),
                                   TLS_RECORD_ALERT, ctx->version, alert, 2);
        if (rec_len > 0) {
            ctx->tls_send(ctx, record, (uint32_t)rec_len);
        }
    }

    ctx->state = TLS_STATE_CLOSED;
    return 0;
}

/*
 * tls_state_str - 获取TLS状态的字符串描述
 */
const char *tls_state_str(tls_state_t state)
{
    static const char *states[] = {
        "CLOSED",
        "CONNECTING",
        "HANDSHAKING",
        "CONNECTED",
        "ERROR"
    };

    if (state <= TLS_STATE_ERROR)
        return states[state];
    return "UNKNOWN";
}

/*
 * tls_error_str - 获取最后的错误信息
 */
const char *tls_error_str(const tls_ctx_t *ctx)
{
    if (!ctx)
        return "NULL context";

    if (ctx->error_msg[0])
        return ctx->error_msg;

    return "No error";
}
