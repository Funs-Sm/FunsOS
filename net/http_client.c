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
