#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "stdint.h"

/* HTTP Methods */
#define HTTP_GET    0
#define HTTP_POST   1
#define HTTP_PUT    2
#define HTTP_DELETE 3

/* HTTP Status Codes */
#define HTTP_STATUS_OK                  200
#define HTTP_STATUS_MOVED_PERMANENTLY   301
#define HTTP_STATUS_FOUND               302
#define HTTP_STATUS_BAD_REQUEST         400
#define HTTP_STATUS_UNAUTHORIZED        401
#define HTTP_STATUS_FORBIDDEN           403
#define HTTP_STATUS_NOT_FOUND           404
#define HTTP_STATUS_INTERNAL_ERROR      500
#define HTTP_STATUS_BAD_GATEWAY         502
#define HTTP_STATUS_SERVICE_UNAVAILABLE 503

typedef struct {
    uint32_t status_code;
    char    *headers;
    uint32_t header_len;
    uint8_t *body;
    uint32_t body_len;
    char     content_type[64];
} http_response_t;

typedef struct {
    uint32_t method;
    char     host[128];
    uint32_t port;
    char     path[256];
    char    *headers;
    uint32_t header_len;
    uint8_t *body;
    uint32_t body_len;
} http_request_t;

void http_client_init(void);
int  http_get(const char *url, http_response_t *response);
int  http_post(const char *url, const void *body, uint32_t body_len,
               http_response_t *response);
void http_free_response(http_response_t *response);

#endif
