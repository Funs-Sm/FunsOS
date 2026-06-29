#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include "stdint.h"

#define FTP_DEFAULT_PORT       21
#define FTP_DATA_PORT_DEFAULT  20
#define FTP_MAX_CMD_LEN        512
#define FTP_MAX_PATH_LEN       512
#define FTP_MAX_USER_LEN       64
#define FTP_MAX_PASS_LEN       64
#define FTP_RECV_BUF_SIZE      4096

#define FTP_STATE_DISCONNECTED  0
#define FTP_STATE_CONNECTED     1
#define FTP_STATE_USER_OK       2
#define FTP_STATE_LOGGED_IN     3

#define FTP_MODE_ACTIVE   0
#define FTP_MODE_PASSIVE  1

typedef struct ftp_server ftp_server_t;
typedef struct ftp_client ftp_client_t;

struct ftp_server {
    uint16_t    port;
    int         running;
    void       *private_data;
    char        root_dir[256];
    char        welcome_msg[256];
};

struct ftp_client {
    void       *ctrl_sock;
    void       *data_sock;
    void       *pasv_listener;
    uint8_t     state;
    uint8_t     mode;
    uint16_t    data_port;
    uint32_t    pasv_port;
    char        user[FTP_MAX_USER_LEN];
    char        pass[FTP_MAX_PASS_LEN];
    char        cwd[FTP_MAX_PATH_LEN];
    char        rename_from[FTP_MAX_PATH_LEN];
    ftp_server_t *server;
    struct ftp_client *next;
};

void ftp_server_init(void);
ftp_server_t *ftp_server_create(uint16_t port);
int  ftp_server_start(ftp_server_t *server);
void ftp_server_stop(ftp_server_t *server);
void ftp_server_destroy(ftp_server_t *server);
int  ftp_server_set_root(ftp_server_t *server, const char *root_dir);
int  ftp_server_set_welcome(ftp_server_t *server, const char *msg);

int  ftp_send_response(void *sock, uint32_t code, const char *msg);
int  ftp_send_response_multi(void *sock, uint32_t code,
                              const char *lines[], uint32_t count);

#endif
