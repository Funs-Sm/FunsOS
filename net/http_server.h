#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "stdint.h"

#define HTTP_SERVER_DEFAULT_PORT  8080
#define HTTP_SERVER_MAX_PATH      512
#define HTTP_SERVER_RECV_BUF_SIZE 4096
#define HTTP_SERVER_MAX_CLIENTS   8

typedef struct http_server http_server_t;

typedef struct {
    char     method[16];
    char     path[HTTP_SERVER_MAX_PATH];
    char     version[16];
    char    *headers;
    uint32_t header_len;
    uint8_t  *body;
    uint32_t body_len;
} http_server_request_t;

typedef struct {
    uint32_t status_code;
    char    *headers;
    uint32_t header_len;
    uint8_t *body;
    uint32_t body_len;
    char     content_type[64];
} http_server_response_t;

typedef int (*http_request_handler_t)(http_server_t *server,
                                       http_server_request_t *req,
                                       http_server_response_t *resp,
                                       void *user_data);

struct http_server {
    uint16_t    port;
    int         running;
    void       *private_data;
    http_request_handler_t handler;
    void       *user_data;
    char        root_dir[256];
};

void http_server_init(void);
http_server_t *http_server_create(uint16_t port);
int  http_server_start(http_server_t *server);
void http_server_stop(http_server_t *server);
void http_server_destroy(http_server_t *server);
int  http_server_set_root(http_server_t *server, const char *root_dir);
int  http_server_set_handler(http_server_t *server,
                              http_request_handler_t handler,
                              void *user_data);

int  http_server_send_response(void *sock, http_server_response_t *resp);
int  http_server_send_file(void *sock, const char *path);
int  http_server_send_error(void *sock, uint32_t status_code, const char *reason);

const char *http_status_reason(uint32_t status_code);
const char *http_mime_type(const char *filename);

#endif
