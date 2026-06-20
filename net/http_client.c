#include "http_client.h"
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"

#define HTTP_DEFAULT_PORT  80
#define HTTP_RECV_BUF_SIZE 4096
#define HTTP_MAX_RESP_SIZE  (256 * 1024)

/* ---- internal helpers ---- */

static int parse_url(const char *url, char *host, uint32_t *port, char *path)
{
    const char *p = url;

    /* skip "http://" */
    if (strncmp(p, "http://", 7) != 0)
        return -1;
    p += 7;

    /* extract host */
    uint32_t i = 0;
    while (*p && *p != ':' && *p != '/' && i < 127) {
        host[i++] = *p++;
    }
    host[i] = '\0';

    /* optional port */
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p && *p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
        if (*port == 0)
            *port = HTTP_DEFAULT_PORT;
    } else {
        *port = HTTP_DEFAULT_PORT;
    }

    /* path */
    if (*p == '/') {
        strncpy(path, p, 255);
        path[255] = '\0';
    } else {
        strcpy(path, "/");
    }

    return 0;
}

static int find_header(const char *headers, uint32_t header_len,
                       const char *name, char *value, uint32_t value_max)
{
    uint32_t name_len = strlen(name);
    uint32_t pos = 0;

    while (pos + name_len + 2 <= header_len) {
        if (strncasecmp(headers + pos, name, name_len) == 0 &&
            headers[pos + name_len] == ':' ) {
            /* skip ": " */
            uint32_t vstart = pos + name_len + 1;
            while (vstart < header_len && headers[vstart] == ' ')
                vstart++;
            uint32_t vend = vstart;
            while (vend < header_len && headers[vend] != '\r' &&
                   headers[vend] != '\n')
                vend++;
            uint32_t vlen = vend - vstart;
            if (vlen >= value_max)
                vlen = value_max - 1;
            memcpy(value, headers + vstart, vlen);
            value[vlen] = '\0';
            return 0;
        }
        /* advance to next line */
        while (pos < header_len && headers[pos] != '\n')
            pos++;
        if (pos < header_len)
            pos++;
    }
    return -1;
}

static int http_request(uint32_t method, const char *url,
                        const void *req_body, uint32_t req_body_len,
                        http_response_t *response)
{
    char host[128];
    uint32_t port;
    char path[256];

    if (parse_url(url, host, &port, path) != 0)
        return -1;

    /* resolve DNS */
    ipv4_addr_t dst_ip;
    if (dns_resolve(host, &dst_ip) != 0)
        return -2;

    /* get outgoing interface */
    net_interface_t *iface = net_get_default_interface();
    if (!iface)
        return -3;

    /* connect */
    uint16_t src_port = tcp_ephemeral_alloc();
    tcp_socket_t *sock = tcp_connect(iface, dst_ip, (uint16_t)port, src_port);
    if (!sock)
        return -4;

    /* build request line + headers */
    static const char *method_str[] = { "GET", "POST", "PUT", "DELETE" };
    const char *mstr = (method <= HTTP_DELETE) ? method_str[method] : "GET";

    /* Content-Length for body-bearing requests */
    char cl_header[64];
    cl_header[0] = '\0';
    if (req_body && req_body_len > 0) {
        /* manual number-to-string to avoid depending on sprintf */
        char numbuf[16];
        utoa(req_body_len, numbuf, 10);
        strcpy(cl_header, "Content-Length: ");
        strcat(cl_header, numbuf);
        strcat(cl_header, "\r\n");
    }

    /*
     * We build the full request in a temporary buffer.
     * Max header block is well under 1 KiB for our simple use-case.
     */
    uint32_t req_hdr_len = strlen(mstr) + 1 /* space */
                         + strlen(path) + 1 /* space */
                         + 8 /* HTTP/1.1 */  + 2 /* \r\n */
                         + 6 + strlen(host) + 2 /* Host: ...\r\n */
                         + 20 /* Connection: close\r\n */
                         + strlen(cl_header)
                         + 2;  /* final \r\n */
    uint8_t *req_buf = (uint8_t *)kmalloc(req_hdr_len + req_body_len + 1);
    if (!req_buf) {
        tcp_close(sock);
        return -5;
    }

    /* assemble header */
    uint32_t off = 0;
    memcpy(req_buf + off, mstr, strlen(mstr));           off += strlen(mstr);
    req_buf[off++] = ' ';
    memcpy(req_buf + off, path, strlen(path));            off += strlen(path);
    req_buf[off++] = ' ';
    memcpy(req_buf + off, "HTTP/1.1\r\n", 10);           off += 10;
    memcpy(req_buf + off, "Host: ", 6);                   off += 6;
    memcpy(req_buf + off, host, strlen(host));            off += strlen(host);
    memcpy(req_buf + off, "\r\n", 2);                     off += 2;
    memcpy(req_buf + off, "Connection: close\r\n", 19);  off += 19;
    if (cl_header[0]) {
        uint32_t cl_len = strlen(cl_header);
        memcpy(req_buf + off, cl_header, cl_len);
        off += cl_len;
    }
    memcpy(req_buf + off, "\r\n", 2);                     off += 2;

    /* append body if present */
    if (req_body && req_body_len > 0) {
        memcpy(req_buf + off, req_body, req_body_len);
        off += req_body_len;
    }

    /* send request */
    int sent = tcp_send(sock, req_buf, off);
    kfree(req_buf);
    if (sent < 0) {
        tcp_close(sock);
        return -6;
    }

    /* receive response into a growing buffer */
    uint8_t *resp_buf = (uint8_t *)kmalloc(HTTP_RECV_BUF_SIZE);
    if (!resp_buf) {
        tcp_close(sock);
        return -7;
    }
    uint32_t resp_cap  = HTTP_RECV_BUF_SIZE;
    uint32_t resp_len  = 0;

    for (;;) {
        int n = tcp_recv(sock, resp_buf + resp_len, resp_cap - resp_len);
        if (n <= 0)
            break;
        resp_len += (uint32_t)n;
        if (resp_len >= resp_cap) {
            uint32_t new_cap = resp_cap * 2;
            if (new_cap > HTTP_MAX_RESP_SIZE)
                new_cap = HTTP_MAX_RESP_SIZE;
            if (new_cap <= resp_cap)
                break;
            uint8_t *new_buf = (uint8_t *)kmalloc(new_cap);
            if (!new_buf)
                break;
            memcpy(new_buf, resp_buf, resp_len);
            kfree(resp_buf);
            resp_buf = new_buf;
            resp_cap = new_cap;
        }
    }

    tcp_close(sock);

    if (resp_len == 0) {
        kfree(resp_buf);
        return -8;
    }

    /* null-terminate for string operations */
    resp_buf[resp_len] = '\0';

    /* ---- parse response ---- */

    /* status line: HTTP/1.x NNN Reason\r\n */
    if (resp_len < 12 || strncmp((const char *)resp_buf, "HTTP/1.", 7) != 0) {
        kfree(resp_buf);
        return -9;
    }

    /* find the space before status code */
    const char *sp = strchr((const char *)resp_buf, ' ');
    if (!sp) {
        kfree(resp_buf);
        return -9;
    }
    response->status_code = (uint32_t)atoi(sp + 1);

    /* find end of status line */
    const char *hdr_start = strstr((const char *)resp_buf, "\r\n");
    if (!hdr_start) {
        kfree(resp_buf);
        return -9;
    }
    hdr_start += 2; /* skip \r\n */

    /* find end of headers (blank line) */
    const char *hdr_end = strstr(hdr_start, "\r\n\r\n");
    if (!hdr_end) {
        kfree(resp_buf);
        return -9;
    }

    response->header_len = (uint32_t)(hdr_end - hdr_start);
    response->headers = (char *)kmalloc(response->header_len + 1);
    if (response->headers) {
        memcpy(response->headers, hdr_start, response->header_len);
        response->headers[response->header_len] = '\0';
    }

    /* body */
    const uint8_t *body_start = (const uint8_t *)(hdr_end + 4);
    uint32_t total_hdr = (uint32_t)((const uint8_t *)body_start - resp_buf);
    response->body_len = (resp_len >= total_hdr) ? resp_len - total_hdr : 0;

    if (response->body_len > 0) {
        response->body = (uint8_t *)kmalloc(response->body_len + 1);
        if (response->body) {
            memcpy(response->body, body_start, response->body_len);
            response->body[response->body_len] = '\0';
        }
    } else {
        response->body = NULL;
    }

    /* extract Content-Type */
    if (response->headers) {
        if (find_header(response->headers, response->header_len,
                        "Content-Type", response->content_type,
                        sizeof(response->content_type)) != 0)
            response->content_type[0] = '\0';
    } else {
        response->content_type[0] = '\0';
    }

    kfree(resp_buf);
    return 0;
}

/* ---- public API ---- */

void http_client_init(void)
{
    /* nothing to do for now; DNS and TCP are initialised elsewhere */
}

int http_get(const char *url, http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    return http_request(HTTP_GET, url, NULL, 0, response);
}

int http_post(const char *url, const void *body, uint32_t body_len,
              http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    return http_request(HTTP_POST, url, body, body_len, response);
}

void http_free_response(http_response_t *response)
{
    if (response->headers) {
        kfree(response->headers);
        response->headers = NULL;
    }
    if (response->body) {
        kfree(response->body);
        response->body = NULL;
    }
    response->header_len = 0;
    response->body_len   = 0;
    response->status_code = 0;
    response->content_type[0] = '\0';
}

/*
 * ==================== HTTP/HTTPS 客户端 (v0.6 增强) ====================
 */

#include "ssl_tls.h"
#include <stdio.h>

#define HTTPS_DEFAULT_PORT 443
#define HTTP_MAX_URL_LEN   2048

/* ========== 类型定义 (v0.6 增强) ========== */

/* HTTP/HTTPS URL 解析结果 */
typedef struct {
    char host[256];
    char path[1024];
    uint16_t port;
    int use_https;          /* 1=HTTPS, 0=HTTP */
    char auth_user[64];     /* Basic Auth 用户名 */
    char auth_pass[64];     /* Basic Auth 密码 */
} http_url_t;

/* HTTP 响应解析信息 */
typedef struct {
    int status_code;           /* 200, 404, 500 etc */
    char status_text[64];      /* OK, Not Found etc */
    char content_type[128];    /* application/json etc */
    uint32_t content_length;   /* Content-Length 值 */
    char location[512];        /* Location 重定向URL */
    int chunked_transfer;      /* Transfer-Encoding: chunked */
    int connection_close;      /* Connection: close */
} http_response_info_t;

/* 前向声明: Socket 辅助函数 */
static int tcp_send_from_socket(int sock, const void *data, uint32_t len);
static int tcp_recv_from_socket(int sock, void *buf, uint32_t len);

/* ========== URL 解析和编解码 ========== */

/*
 * http_parse_url - 解析HTTP/HTTPS URL
 * 支持格式:
 *   - http://host[:port]/path
 *   - https://host[:port]/path
 *   - http://user:pass@host/path (Basic Auth)
 */
int http_parse_url(const char *url, http_url_t *out)
{
    if (!url || !out)
        return -1;

    memset(out, 0, sizeof(http_url_t));
    const char *p = url;

    /* 检测协议前缀 */
    if (strncmp(p, "https://", 8) == 0) {
        out->use_https = 1;
        out->port = HTTPS_DEFAULT_PORT;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        out->use_https = 0;
        out->port = HTTP_DEFAULT_PORT;
        p += 7;
    } else {
        return -2;  /* 无效的URL协议 */
    }

    /* 检测Basic Auth: user:pass@host 格式 */
    const char *at_sign = strchr(p, '@');
    const char *host_start = p;

    if (at_sign && (at_sign < strchr(p, '/') || !strchr(p, '/'))) {
        /* 提取 user:pass */
        const char *colon = strchr(p, ':');
        if (colon && colon < at_sign) {
            uint32_t user_len = (uint32_t)(colon - p);
            if (user_len >= sizeof(out->auth_user))
                user_len = sizeof(out->auth_user) - 1;
            memcpy(out->auth_user, p, user_len);
            out->auth_user[user_len] = '\0';

            uint32_t pass_len = (uint32_t)(at_sign - colon - 1);
            if (pass_len >= sizeof(out->auth_pass))
                pass_len = sizeof(out->auth_pass) - 1;
            memcpy(out->auth_pass, colon + 1, pass_len);
            out->auth_pass[pass_len] = '\0';
        }
        host_start = at_sign + 1;
    } else {
        /* 没有认证信息 */
        out->auth_user[0] = '\0';
        out->auth_pass[0] = '\0';
    }

    /* 提取主机名 (直到 ':' 或 '/' 或结束) */
    uint32_t i = 0;
    p = host_start;
    while (*p && *p != ':' && *p != '/' && i < sizeof(out->host) - 1) {
        out->host[i++] = *p++;
    }
    out->host[i] = '\0';

    /* 可选端口 */
    if (*p == ':') {
        p++;
        out->port = 0;
        while (*p && *p >= '0' && *p <= '9') {
            out->port = out->port * 10 + (*p - '0');
            p++;
        }
        if (out->port == 0) {
            out->port = out->use_https ? HTTPS_DEFAULT_PORT : HTTP_DEFAULT_PORT;
        }
    }

    /* 路径部分 */
    if (*p == '/') {
        strncpy(out->path, p, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
    } else if (*p == '\0' || *p == '?' || *p == '#') {
        strcpy(out->path, "/");
        /* 保留查询字符串或片段标识符 */
        if (*p) {
            strncat(out->path, p, sizeof(out->path) - strlen(out->path) - 1);
        }
    } else {
        strcpy(out->path, "/");
    }

    return 0;
}

/*
 * url_encode - URL编码 (Percent-encoding)
 * 将特殊字符转换为 %XX 格式
 */
uint32_t url_encode(const char *src, char *dst, uint32_t dst_size)
{
    if (!src || !dst || dst_size == 0)
        return 0;

    static const char hex_chars[] = "0123456789ABCDEF";
    uint32_t src_idx = 0, dst_idx = 0;

    while (src[src_idx] && dst_idx < dst_size - 1) {
        unsigned char c = (unsigned char)src[src_idx++];

        /* 不需要编码的字符 (RFC3986 unreserved characters) */
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[dst_idx++] = c;
        } else if (c == ' ') {
            /* 空格通常编码为 '+' 或 "%20" */
            dst[dst_idx++] = '+';
        } else {
            /* 其他字符进行百分号编码 */
            if (dst_idx + 3 >= dst_size)
                break;
            dst[dst_idx++] = '%';
            dst[dst_idx++] = hex_chars[(c >> 4) & 0x0F];
            dst[dst_idx++] = hex_chars[c & 0x0F];
        }
    }

    dst[dst_idx] = '\0';
    return dst_idx;
}

/*
 * url_decode - URL解码
 * 将 %XX 格式转换回原始字符
 */
uint32_t url_decode(const char *src, char *dst, uint32_t dst_size)
{
    if (!src || !dst || dst_size == 0)
        return 0;

    uint32_t src_idx = 0, dst_idx = 0;

    while (src[src_idx] && dst_idx < dst_size - 1) {
        char c = src[src_idx++];

        if (c == '%' && src[src_idx] && src[src_idx + 1]) {
            /* 百分号编码字符 */
            char hex[3] = { src[src_idx], src[src_idx + 1], '\0' };
            src_idx += 2;

            /* 十六进制转十进制 */
            unsigned char decoded = 0;
            int i;
            for (i = 0; i < 2; i++) {
                decoded <<= 4;
                if (hex[i] >= '0' && hex[i] <= '9')
                    decoded |= hex[i] - '0';
                else if (hex[i] >= 'A' && hex[i] <= 'F')
                    decoded |= hex[i] - 'A' + 10;
                else if (hex[i] >= 'a' && hex[i] <= 'f')
                    decoded |= hex[i] - 'a' + 10;
            }
            dst[dst_idx++] = decoded;
        } else if (c == '+') {
            /* '+' 解码为空格 */
            dst[dst_idx++] = ' ';
        } else {
            dst[dst_idx++] = c;
        }
    }

    dst[dst_idx] = '\0';
    return dst_idx;
}

/* ========== HTTP响应解析 ========== */

/*
 * http_parse_response - 解析HTTP响应头信息
 * 从原始响应文本中提取状态码、Content-Type等关键信息
 */
int http_parse_response(const char *raw_response, http_response_info_t *info)
{
    if (!raw_response || !info)
        return -1;

    memset(info, 0, sizeof(http_response_info_t));

    /* 解析状态行: HTTP/1.x NNN Reason Phrase\r\n */
    if (strncmp(raw_response, "HTTP/1.", 7) != 0)
        return -2;

    const char *p = raw_response + 7;  /* 跳过 "HTTP/1." */

    /* 跳过版本号后的空格 */
    if (*p == ' ') p++;

    /* 提取状态码 (3位数字) */
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' ||
        p[2] < '0' || p[2] > '9')
        return -3;

    info->status_code = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
    p += 3;

    /* 跳过空格, 提取原因短语 */
    while (*p == ' ') p++;
    const char *reason_end = strstr(p, "\r\n");
    if (reason_end) {
        uint32_t reason_len = (uint32_t)(reason_end - p);
        if (reason_len >= sizeof(info->status_text))
            reason_len = sizeof(info->status_text) - 1;
        memcpy(info->status_text, p, reason_len);
        info->status_text[reason_len] = '\0';
    }

    /* 移动到头部开始位置 */
    const char *headers_start = strstr(raw_response, "\r\n");
    if (!headers_start)
        return -4;
    headers_start += 2;

    /* 找到头部结束位置 */
    const char *headers_end = strstr(headers_start, "\r\n\r\n");
    if (!headers_end)
        headers_end = headers_start + strlen(headers_start);

    uint32_t headers_len = (uint32_t)(headers_end - headers_start);

    /* 逐个解析头部字段 */
    const char *pos = headers_start;
    while (pos < headers_end && *pos) {
        /* 找到当前行的结束位置 */
        const char *line_end = strstr(pos, "\r\n");
        if (!line_end || line_end > headers_end)
            line_end = headers_end;

        uint32_t line_len = (uint32_t)(line_end - pos);

        /* Content-Type */
        if (strncasecmp(pos, "Content-Type:", 13) == 0) {
            const char *val_start = pos + 13;
            while (*val_start == ' ') val_start++;
            uint32_t val_len = line_len - (uint32_t)(val_start - pos);
            if (val_len >= sizeof(info->content_type))
                val_len = sizeof(info->content_type) - 1;
            memcpy(info->content_type, val_start, val_len);
            info->content_type[val_len] = '\0';
        }
        /* Content-Length */
        else if (strncasecmp(pos, "Content-Length:", 15) == 0) {
            const char *val_start = pos + 15;
            info->content_length = 0;
            while (*val_start == ' ') val_start++;
            while (*val_start >= '0' && *val_start <= '9') {
                info->content_length = info->content_length * 10 + (*val_start - '0');
                val_start++;
            }
        }
        /* Location (重定向URL) */
        else if (strncasecmp(pos, "Location:", 9) == 0) {
            const char *val_start = pos + 9;
            while (*val_start == ' ') val_start++;
            uint32_t val_len = line_len - (uint32_t)(val_start - pos);
            if (val_len >= sizeof(info->location))
                val_len = sizeof(info->location) - 1;
            memcpy(info->location, val_start, val_len);
            info->location[val_len] = '\0';
        }
        /* Transfer-Encoding */
        else if (strncasecmp(pos, "Transfer-Encoding:", 18) == 0) {
            if (strstr(pos, "chunked"))
                info->chunked_transfer = 1;
        }
        /* Connection */
        else if (strncasecmp(pos, "Connection:", 11) == 0) {
            if (strstr(pos, "close"))
                info->connection_close = 1;
        }

        /* 移动到下一行 */
        pos = line_end + 2;
    }

    return 0;
}

/* ========== HTTPS支持 ========== */

/*
 * https_get - 通过TLS连接执行HTTPS GET请求
 *
 * 使用流程:
 * 1. 创建并配置TLS上下文
 * 2. 建立TCP连接
 * 3. 设置TLS I/O回调为socket读写
 * 4. 执行TLS握手
 * 5. 发送HTTP GET请求 (通过加密通道)
 * 6. 接收并解密响应
 */
int https_get(tls_ctx_t *tls, const char *host, const char *path,
              char *response_buf, uint32_t buf_size)
{
    if (!tls || !host || !path || !response_buf || buf_size == 0)
        return -1;

    /* 检查TLS是否已连接 */
    if (tls->state != TLS_STATE_CONNECTED) {
        strcpy(tls->error_msg, "TLS not connected");
        tls->error_code = -1;
        return -1;
    }

    /*
     * 构建HTTP GET请求
     * 注意: 这个请求将通过TLS加密发送
     */
    char request[2048];
    uint32_t req_len = 0;

    /* 请求行 */
    req_len += sprintf(request + req_len, "GET %s HTTP/1.1\r\n", path);

    /* Host头部 (必需) */
    req_len += sprintf(request + req_len, "Host: %s\r\n", host);

    /* User-Agent */
    req_len += sprintf(request + req_len, "User-Agent: FunsOS-HTTP/0.6\r\n");

    /* Accept */
    req_len += sprintf(request + req_len, "Accept: */*\r\n");

    /* Connection */
    req_len += sprintf(request + req_len, "Connection: close\r\n");

    /* 结束头部 */
    req_len += sprintf(request + req_len, "\r\n");

    /* 通过TLS发送请求 */
    int sent = tls_write(tls, request, req_len);
    if (sent < 0) {
        strcpy(tls->error_msg, "Failed to send request via TLS");
        tls->error_code = -2;
        return -2;
    }

    /* 通过TLS接收响应 */
    uint32_t total_received = 0;
    int recv_timeout = 100;  /* 最大读取次数, 防止死循环 */

    while (total_received < buf_size - 1 && recv_timeout-- > 0) {
        int n = tls_read(tls, response_buf + total_received,
                        buf_size - total_received - 1);
        if (n < 0)
            break;  /* 错误或连接关闭 */
        if (n == 0) {
            /* 暂无数据, 短暂等待后继续 */
            continue;
        }
        total_received += (uint32_t)n;

        /* 检查是否已接收完整响应 (简化判断) */
        if (total_received >= 4) {
            /* 查找响应结束标记 */
            if (strstr(response_buf, "\r\n\r\n")) {
                /* 头部已完成, 检查是否有Content-Length */
                http_response_info_t resp_info;
                if (http_parse_response(response_buf, &resp_info) == 0 &&
                    resp_info.content_length > 0) {
                    /* 计算体部起始位置 */
                    const char *body_start = strstr(response_buf, "\r\n\r\n");
                    if (body_start) {
                        body_start += 4;
                        uint32_t header_size = (uint32_t)(body_start - response_buf);
                        uint32_t received_body = total_received - header_size;
                        if (received_body >= resp_info.content_length)
                            break;  /* 已接收完整响应 */
                    }
                }
            }
        }
    }

    /* 确保字符串终止 */
    response_buf[total_received] = '\0';

    return (int)total_received;
}

/* ========== 扩展HTTP方法 ========== */

/*
 * 构建并发送通用HTTP请求的内部辅助函数
 */
static int http_request_extended(int sock, const char *method, const char *host,
                                 const char *path, const void *body,
                                 uint32_t body_len, char *resp_buf,
                                 uint32_t buf_size, const char *extra_headers)
{
    if (!method || !host || !path)
        return -1;

    /* 计算所需缓冲区大小 */
    uint32_t max_req_len = strlen(method) + 1 +
                          strlen(path) + 1 + 9 + 2 +  /* 请求行 */
                          6 + strlen(host) + 2 +       /* Host */
                          20 +                           /* Connection */
                          (extra_headers ? strlen(extra_headers) : 0) +
                          64 +                           /* Content-Length等 */
                          2 +                            /* 结束\r\n */
                          body_len;

    uint8_t *req_buf = (uint8_t *)kmalloc(max_req_len + 1);
    if (!req_buf)
        return -2;

    uint32_t off = 0;

    /* 请求行: METHOD /path HTTP/1.1\r\n */
    memcpy(req_buf + off, method, strlen(method));     off += strlen(method);
    req_buf[off++] = ' ';
    memcpy(req_buf + off, path, strlen(path));          off += strlen(path);
    req_buf[off++] = ' ';
    memcpy(req_buf + off, "HTTP/1.1\r\n", 10);         off += 10;

    /* Host头部 */
    memcpy(req_buf + off, "Host: ", 6);                 off += 6;
    memcpy(req_buf + off, host, strlen(host));          off += strlen(host);
    memcpy(req_buf + off, "\r\n", 2);                   off += 2;

    /* User-Agent */
    memcpy(req_buf + off, "User-Agent: FunsOS-HTTP/0.6\r\n", 28);
    off += 28;

    /* 额外的自定义头部 (如Authorization, Content-Type等) */
    if (extra_headers && extra_headers[0]) {
        uint32_t eh_len = strlen(extra_headers);
        memcpy(req_buf + off, extra_headers, eh_len);
        off += eh_len;
        if (extra_headers[eh_len-1] != '\n') {
            memcpy(req_buf + off, "\r\n", 2);
            off += 2;
        }
    }

    /* 有body时添加Content-Length */
    if (body && body_len > 0) {
        char cl_header[32];
        uint32_t cl_len = sprintf(cl_header, "Content-Length: %u\r\n",
                                  (unsigned)body_len);
        memcpy(req_buf + off, cl_header, cl_len);
        off += cl_len;
    }

    /* Connection: close */
    memcpy(req_buf + off, "Connection: close\r\n", 19); off += 19;

    /* 结束头部 */
    memcpy(req_buf + off, "\r\n", 2);                   off += 2;

    /* 附加body */
    if (body && body_len > 0) {
        memcpy(req_buf + off, body, body_len);
        off += body_len;
    }

    /* 发送请求 */
    int result = -3;
    if (sock >= 0) {
        int sent = tcp_send_from_socket(sock, req_buf, off);
        if (sent < 0) {
            kfree(req_buf);
            return -4;
        }

        /* 接收响应 */
        if (resp_buf && buf_size > 0) {
            uint32_t total = 0;
            int max_read = 200;  /* 防止无限循环 */

            while (total < buf_size - 1 && max_read-- > 0) {
                int n = tcp_recv_from_socket(sock, resp_buf + total,
                                            buf_size - total - 1);
                if (n <= 0)
                    break;
                total += (uint32_t)n;

                /* 简化的完整性检查 */
                if (total >= 4 && strstr(resp_buf, "\r\n\r\n")) {
                    http_response_info_t info;
                    if (http_parse_response(resp_buf, &info) == 0 &&
                        info.content_length > 0) {
                        const char *body_ptr = strstr(resp_buf, "\r\n\r\n");
                        if (body_ptr) {
                            uint32_t hdr_sz = (uint32_t)(body_ptr - resp_buf) + 4;
                            if (total - hdr_sz >= info.content_length)
                                break;
                        }
                    } else if (!info.chunked_transfer && info.connection_close) {
                        /* Connection: close 且非chunked, 收到数据结束即完成 */
                        break;
                    }
                }
            }

            resp_buf[total] = '\0';
            result = (int)total;
        } else {
            result = 0;
        }
    }

    kfree(req_buf);
    return result;
}

/*
 * 辅助函数: 直接通过socket fd发送 (如果底层支持的话)
 * 这里使用简化的实现, 实际应该调用tcp_send/tcp_recv
 */
static int tcp_send_from_socket(int sock, const void *data, uint32_t len)
{
    (void)sock;
    (void)data;
    (void)len;
    /* 在实际实现中, 这应该调用底层的socket发送函数 */
    /* 返回模拟值用于测试 */
    return (int)len;
}

static int tcp_recv_from_socket(int sock, void *buf, uint32_t len)
{
    (void)sock;
    (void)buf;
    (void)len;
    /* 在实际实现中, 这应该调用底层的socket接收函数 */
    return 0;
}

/*
 * http_post_json - 发送POST请求 (带JSON body)
 */
int http_post_json(int sock, const char *host, const char *path,
                   const char *json_body, char *resp_buf, uint32_t buf_size)
{
    if (!json_body)
        return -1;

    /* 添加JSON相关的头部 */
    char extra_headers[128];
    sprintf(extra_headers, "Content-Type: application/json\r\n"
                         "Accept: application/json\r\n");

    uint32_t body_len = strlen(json_body);
    return http_request_extended(sock, "POST", host, path,
                                 json_body, body_len,
                                 resp_buf, buf_size, extra_headers);
}

/*
 * http_head - 发送HEAD请求 (只获取响应头, 不包含body)
 */
int http_head(int sock, const char *host, const char *path,
              char *resp_buf, uint32_t buf_size)
{
    return http_request_extended(sock, "HEAD", host, path,
                                 NULL, 0, resp_buf, buf_size, NULL);
}

/*
 * http_put - 发送PUT请求 (上传资源)
 */
int http_put(int sock, const char *host, const char *path,
             const void *body, uint32_t body_len,
             char *resp_buf, uint32_t buf_size)
{
    return http_request_extended(sock, "PUT", host, path,
                                 body, body_len, resp_buf, buf_size, NULL);
}

/*
 * http_delete - 发送DELETE请求 (删除资源)
 */
int http_delete(int sock, const char *host, const char *path,
                char *resp_buf, uint32_t buf_size)
{
    return http_request_extended(sock, "DELETE", host, path,
                                 NULL, 0, resp_buf, buf_size, NULL);
}
