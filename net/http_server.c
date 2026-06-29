#include "http_server.h"
#include "tcp.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "vfs.h"

#define HTTP_SERVER_DEFAULT_ROOT "/"

static const struct {
    uint32_t code;
    const char *reason;
} http_status_table[] = {
    { 200, "OK" },
    { 201, "Created" },
    { 204, "No Content" },
    { 301, "Moved Permanently" },
    { 302, "Found" },
    { 304, "Not Modified" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 503, "Service Unavailable" },
    { 0, NULL }
};

static const struct {
    const char *ext;
    const char *mime;
} mime_table[] = {
    { "html", "text/html" },
    { "htm",  "text/html" },
    { "css",  "text/css" },
    { "js",   "application/javascript" },
    { "json", "application/json" },
    { "xml",  "application/xml" },
    { "txt",  "text/plain" },
    { "png",  "image/png" },
    { "jpg",  "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "gif",  "image/gif" },
    { "bmp",  "image/bmp" },
    { "ico",  "image/x-icon" },
    { "svg",  "image/svg+xml" },
    { "pdf",  "application/pdf" },
    { "zip",  "application/zip" },
    { "gz",   "application/gzip" },
    { "mp3",  "audio/mpeg" },
    { "mp4",  "video/mp4" },
    { "wav",  "audio/wav" },
    { NULL,   "application/octet-stream" }
};

const char *http_status_reason(uint32_t status_code)
{
    for (int i = 0; http_status_table[i].reason; i++) {
        if (http_status_table[i].code == status_code)
            return http_status_table[i].reason;
    }
    return "Unknown";
}

const char *http_mime_type(const char *filename)
{
    if (!filename)
        return "application/octet-stream";

    const char *ext = strrchr(filename, '.');
    if (!ext || !*(ext + 1))
        return "application/octet-stream";
    ext++;

    for (int i = 0; mime_table[i].ext; i++) {
        if (strcasecmp(ext, mime_table[i].ext) == 0)
            return mime_table[i].mime;
    }
    return "application/octet-stream";
}

void http_server_init(void)
{
}

http_server_t *http_server_create(uint16_t port)
{
    http_server_t *server = (http_server_t *)kmalloc(sizeof(http_server_t));
    if (!server)
        return NULL;

    memset(server, 0, sizeof(http_server_t));
    server->port = port ? port : HTTP_SERVER_DEFAULT_PORT;
    server->running = 0;
    strcpy(server->root_dir, HTTP_SERVER_DEFAULT_ROOT);
    server->handler = NULL;
    server->user_data = NULL;

    return server;
}

int http_server_set_root(http_server_t *server, const char *root_dir)
{
    if (!server || !root_dir)
        return -1;

    strncpy(server->root_dir, root_dir, sizeof(server->root_dir) - 1);
    server->root_dir[sizeof(server->root_dir) - 1] = '\0';
    return 0;
}

int http_server_set_handler(http_server_t *server,
                             http_request_handler_t handler,
                             void *user_data)
{
    if (!server)
        return -1;

    server->handler = handler;
    server->user_data = user_data;
    return 0;
}

static int parse_http_request(const char *buf, uint32_t len,
                               http_server_request_t *req)
{
    if (!buf || !req || len < 10)
        return -1;

    memset(req, 0, sizeof(http_server_request_t));

    const char *p = buf;
    const char *end = buf + len;

    const char *sp1 = memchr(p, ' ', end - p);
    if (!sp1) return -1;
    uint32_t method_len = (uint32_t)(sp1 - p);
    if (method_len >= sizeof(req->method))
        method_len = sizeof(req->method) - 1;
    memcpy(req->method, p, method_len);
    req->method[method_len] = '\0';
    p = sp1 + 1;

    const char *sp2 = memchr(p, ' ', end - p);
    if (!sp2) return -1;
    uint32_t path_len = (uint32_t)(sp2 - p);
    if (path_len >= sizeof(req->path))
        path_len = sizeof(req->path) - 1;
    memcpy(req->path, p, path_len);
    req->path[path_len] = '\0';
    p = sp2 + 1;

    const char *eol = memchr(p, '\n', end - p);
    if (!eol) return -1;
    uint32_t ver_len = (uint32_t)(eol - p);
    if (eol > p && *(eol - 1) == '\r')
        ver_len--;
    if (ver_len >= sizeof(req->version))
        ver_len = sizeof(req->version) - 1;
    memcpy(req->version, p, ver_len);
    req->version[ver_len] = '\0';
    p = eol + 1;

    const char *hdr_end = strstr(p, "\r\n\r\n");
    if (!hdr_end)
        hdr_end = strstr(p, "\n\n");
    if (!hdr_end)
        hdr_end = end;

    req->header_len = (uint32_t)(hdr_end - p);
    if (req->header_len > 0) {
        req->headers = (char *)kmalloc(req->header_len + 1);
        if (req->headers) {
            memcpy(req->headers, p, req->header_len);
            req->headers[req->header_len] = '\0';
        }
    }

    if (hdr_end + 4 <= end) {
        const char *body_start = hdr_end + 4;
        req->body_len = (uint32_t)(end - body_start);
        if (req->body_len > 0) {
            req->body = (uint8_t *)kmalloc(req->body_len);
            if (req->body) {
                memcpy(req->body, body_start, req->body_len);
            }
        }
    }

    return 0;
}

static void free_request_data(http_server_request_t *req)
{
    if (req->headers) {
        kfree(req->headers);
        req->headers = NULL;
    }
    if (req->body) {
        kfree(req->body);
        req->body = NULL;
    }
    req->header_len = 0;
    req->body_len = 0;
}

static int default_file_handler(http_server_t *server,
                                 http_server_request_t *req,
                                 http_server_response_t *resp)
{
    char full_path[HTTP_SERVER_MAX_PATH + 256];

    if (strcmp(req->method, "GET") != 0) {
        resp->status_code = 405;
        return 0;
    }

    const char *path = req->path;
    if (path[0] == '\0' || strcmp(path, "/") == 0)
        path = "/index.html";

    if (strstr(path, "..")) {
        resp->status_code = 403;
        return 0;
    }

    snprintf(full_path, sizeof(full_path), "%s%s", server->root_dir, path);

    file_t *file = NULL;
    int32_t ret = vfs_open(full_path, FILE_MODE_READ, &file);
    if (ret < 0 || !file) {
        resp->status_code = 404;
        return 0;
    }

    inode_t stat;
    if (vfs_stat(full_path, &stat) == 0 && (stat.mode & FILE_MODE_DIR)) {
        vfs_close(file);
        resp->status_code = 403;
        return 0;
    }

    uint32_t file_size = file->inode ? file->inode->size : 0;
    if (file_size == 0) {
        vfs_seek(file, 0, SEEK_END);
        file_size = file->offset;
        vfs_seek(file, 0, SEEK_SET);
    }

    if (file_size > 4 * 1024 * 1024) {
        vfs_close(file);
        resp->status_code = 500;
        return 0;
    }

    uint8_t *file_buf = (uint8_t *)kmalloc(file_size);
    if (!file_buf) {
        vfs_close(file);
        resp->status_code = 500;
        return 0;
    }

    int32_t bytes_read = vfs_read(file, file_buf, file_size);
    vfs_close(file);

    if (bytes_read <= 0) {
        kfree(file_buf);
        resp->status_code = 500;
        return 0;
    }

    resp->status_code = 200;
    resp->body = file_buf;
    resp->body_len = (uint32_t)bytes_read;
    strncpy(resp->content_type, http_mime_type(full_path),
            sizeof(resp->content_type) - 1);

    return 0;
}

int http_server_send_response(void *sock, http_server_response_t *resp)
{
    if (!sock || !resp)
        return -1;

    tcp_socket_t *ts = (tcp_socket_t *)sock;
    char header_buf[2048];
    uint32_t hdr_len = 0;

    const char *reason = http_status_reason(resp->status_code);

    hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                        "HTTP/1.0 %u %s\r\n", resp->status_code, reason);

    if (resp->content_type[0]) {
        hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                            "Content-Type: %s\r\n", resp->content_type);
    }

    hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                        "Content-Length: %u\r\n", resp->body_len);

    hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                        "Connection: close\r\n");
    hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                        "Server: FunsOS-HTTP/1.0\r\n");

    hdr_len += snprintf(header_buf + hdr_len, sizeof(header_buf) - hdr_len,
                        "\r\n");

    int sent = tcp_send(ts, header_buf, hdr_len);
    if (sent < 0)
        return -1;

    if (resp->body && resp->body_len > 0) {
        sent = tcp_send(ts, resp->body, resp->body_len);
        if (sent < 0)
            return -1;
    }

    return 0;
}

int http_server_send_error(void *sock, uint32_t status_code, const char *reason)
{
    if (!sock)
        return -1;

    http_server_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.status_code = status_code;

    char body[512];
    if (!reason)
        reason = http_status_reason(status_code);

    int body_len = snprintf(body, sizeof(body),
        "<html><head><title>%u %s</title></head>"
        "<body><h1>%u %s</h1></body></html>",
        status_code, reason, status_code, reason);

    resp.body = (uint8_t *)body;
    resp.body_len = (uint32_t)body_len;
    strcpy(resp.content_type, "text/html");

    return http_server_send_response(sock, &resp);
}

int http_server_send_file(void *sock, const char *path)
{
    if (!sock || !path)
        return -1;

    file_t *file = NULL;
    int32_t ret = vfs_open(path, FILE_MODE_READ, &file);
    if (ret < 0 || !file)
        return -1;

    uint32_t file_size = file->inode ? file->inode->size : 0;
    if (file_size == 0) {
        vfs_seek(file, 0, SEEK_END);
        file_size = file->offset;
        vfs_seek(file, 0, SEEK_SET);
    }

    uint8_t *buf = (uint8_t *)kmalloc(file_size + 1024);
    if (!buf) {
        vfs_close(file);
        return -1;
    }

    http_server_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.status_code = 200;
    strncpy(resp.content_type, http_mime_type(path), sizeof(resp.content_type) - 1);

    int32_t bytes_read = vfs_read(file, buf + 1024, file_size);
    vfs_close(file);

    if (bytes_read <= 0) {
        kfree(buf);
        return -1;
    }

    resp.body = buf + 1024;
    resp.body_len = (uint32_t)bytes_read;

    int result = http_server_send_response(sock, &resp);
    kfree(buf);
    return result;
}

static void handle_client(http_server_t *server, tcp_socket_t *client)
{
    uint8_t *recv_buf = (uint8_t *)kmalloc(HTTP_SERVER_RECV_BUF_SIZE);
    if (!recv_buf) {
        tcp_close(client);
        return;
    }

    uint32_t total_recv = 0;
    int timeout = 100;

    while (total_recv < HTTP_SERVER_RECV_BUF_SIZE - 1 && timeout-- > 0) {
        int n = tcp_recv(client, recv_buf + total_recv,
                         HTTP_SERVER_RECV_BUF_SIZE - 1 - total_recv);
        if (n <= 0)
            break;
        total_recv += (uint32_t)n;

        if (strstr((char *)recv_buf, "\r\n\r\n") ||
            strstr((char *)recv_buf, "\n\n"))
            break;
    }

    if (total_recv == 0) {
        kfree(recv_buf);
        tcp_close(client);
        return;
    }

    recv_buf[total_recv] = '\0';

    http_server_request_t req;
    if (parse_http_request((char *)recv_buf, total_recv, &req) != 0) {
        http_server_send_error(client, 400, "Bad Request");
        kfree(recv_buf);
        tcp_close(client);
        return;
    }

    http_server_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.status_code = 200;

    if (server->handler) {
        server->handler(server, &req, &resp, server->user_data);
    } else {
        default_file_handler(server, &req, &resp);
    }

    if (resp.status_code >= 400 && !resp.body) {
        http_server_send_error(client, resp.status_code, NULL);
    } else {
        http_server_send_response(client, &resp);
    }

    if (resp.body && resp.body_len > 0)
        kfree(resp.body);
    if (resp.headers)
        kfree(resp.headers);

    free_request_data(&req);
    kfree(recv_buf);
    tcp_close(client);
}

int http_server_start(http_server_t *server)
{
    if (!server || server->running)
        return -1;

    tcp_socket_t *listener = tcp_listen(server->port);
    if (!listener)
        return -1;

    server->private_data = listener;
    server->running = 1;

    return 0;
}

void http_server_stop(http_server_t *server)
{
    if (!server || !server->running)
        return;

    tcp_socket_t *listener = (tcp_socket_t *)server->private_data;
    if (listener) {
        tcp_close(listener);
    }

    server->running = 0;
    server->private_data = NULL;
}

void http_server_destroy(http_server_t *server)
{
    if (!server)
        return;

    if (server->running)
        http_server_stop(server);

    kfree(server);
}
