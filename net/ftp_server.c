#include "ftp_server.h"
#include "tcp.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "vfs.h"

#define FTP_DEFAULT_ROOT "/"
#define FTP_DEFAULT_WELCOME "220 FunsOS FTP Server ready."

int ftp_send_response(void *sock, uint32_t code, const char *msg)
{
    if (!sock || !msg)
        return -1;

    tcp_socket_t *ts = (tcp_socket_t *)sock;
    char buf[FTP_MAX_CMD_LEN + 32];
    int len = snprintf(buf, sizeof(buf), "%u %s\r\n", code, msg);
    if (len <= 0)
        return -1;

    return tcp_send(ts, buf, (uint32_t)len);
}

int ftp_send_response_multi(void *sock, uint32_t code,
                             const char *lines[], uint32_t count)
{
    if (!sock || !lines || count == 0)
        return -1;

    tcp_socket_t *ts = (tcp_socket_t *)sock;
    char buf[FTP_MAX_CMD_LEN + 32];
    int total_sent = 0;

    for (uint32_t i = 0; i < count; i++) {
        int len;
        if (i == count - 1) {
            len = snprintf(buf, sizeof(buf), "%u %s\r\n", code, lines[i]);
        } else {
            len = snprintf(buf, sizeof(buf), "%u-%s\r\n", code, lines[i]);
        }
        if (len <= 0)
            return -1;
        int sent = tcp_send(ts, buf, (uint32_t)len);
        if (sent < 0)
            return -1;
        total_sent += sent;
    }

    return total_sent;
}

void ftp_server_init(void)
{
}

ftp_server_t *ftp_server_create(uint16_t port)
{
    ftp_server_t *server = (ftp_server_t *)kmalloc(sizeof(ftp_server_t));
    if (!server)
        return NULL;

    memset(server, 0, sizeof(ftp_server_t));
    server->port = port ? port : FTP_DEFAULT_PORT;
    server->running = 0;
    strcpy(server->root_dir, FTP_DEFAULT_ROOT);
    strcpy(server->welcome_msg, FTP_DEFAULT_WELCOME);

    return server;
}

int ftp_server_set_root(ftp_server_t *server, const char *root_dir)
{
    if (!server || !root_dir)
        return -1;

    strncpy(server->root_dir, root_dir, sizeof(server->root_dir) - 1);
    server->root_dir[sizeof(server->root_dir) - 1] = '\0';
    return 0;
}

int ftp_server_set_welcome(ftp_server_t *server, const char *msg)
{
    if (!server || !msg)
        return -1;

    strncpy(server->welcome_msg, msg, sizeof(server->welcome_msg) - 1);
    server->welcome_msg[sizeof(server->welcome_msg) - 1] = '\0';
    return 0;
}

static void build_full_path(ftp_client_t *client, const char *path,
                             char *full_path, uint32_t max_len)
{
    if (!path || path[0] == '\0') {
        snprintf(full_path, max_len, "%s%s",
                 client->server->root_dir, client->cwd);
    } else if (path[0] == '/') {
        snprintf(full_path, max_len, "%s%s",
                 client->server->root_dir, path);
    } else {
        if (client->cwd[strlen(client->cwd) - 1] == '/') {
            snprintf(full_path, max_len, "%s%s%s",
                     client->server->root_dir, client->cwd, path);
        } else {
            snprintf(full_path, max_len, "%s%s/%s",
                     client->server->root_dir, client->cwd, path);
        }
    }
}

static int path_is_allowed(ftp_client_t *client, const char *full_path)
{
    uint32_t root_len = strlen(client->server->root_dir);
    if (strncmp(full_path, client->server->root_dir, root_len) != 0)
        return 0;
    return 1;
}

static int ftp_cmd_user(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    strncpy(client->user, arg, sizeof(client->user) - 1);
    client->user[sizeof(client->user) - 1] = '\0';
    client->state = FTP_STATE_USER_OK;

    ftp_send_response(client->ctrl_sock, 331, "Password required for user.");
    return 0;
}

static int ftp_cmd_pass(ftp_client_t *client, const char *arg)
{
    if (client->state != FTP_STATE_USER_OK) {
        ftp_send_response(client->ctrl_sock, 503, "Login with USER first.");
        return -1;
    }

    if (arg) {
        strncpy(client->pass, arg, sizeof(client->pass) - 1);
        client->pass[sizeof(client->pass) - 1] = '\0';
    }

    client->state = FTP_STATE_LOGGED_IN;
    strcpy(client->cwd, "/");

    ftp_send_response(client->ctrl_sock, 230, "User logged in, proceed.");
    return 0;
}

static int ftp_cmd_quit(ftp_client_t *client, const char *arg)
{
    (void)arg;
    ftp_send_response(client->ctrl_sock, 221, "Goodbye.");
    client->state = FTP_STATE_DISCONNECTED;
    return 0;
}

static int ftp_cmd_syst(ftp_client_t *client, const char *arg)
{
    (void)arg;
    ftp_send_response(client->ctrl_sock, 215, "FunsOS Type: L8");
    return 0;
}

static int ftp_cmd_pwd(ftp_client_t *client, const char *arg)
{
    (void)arg;
    char buf[FTP_MAX_PATH_LEN + 32];
    snprintf(buf, sizeof(buf), "\"%s\" is the current directory.", client->cwd);
    ftp_send_response(client->ctrl_sock, 257, buf);
    return 0;
}

static int ftp_cmd_cwd(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    inode_t stat;
    if (vfs_stat(full_path, &stat) != 0 || !(stat.mode & FILE_MODE_DIR)) {
        ftp_send_response(client->ctrl_sock, 550, "Failed to change directory.");
        return -1;
    }

    if (arg[0] == '/') {
        strncpy(client->cwd, arg, sizeof(client->cwd) - 1);
    } else {
        if (client->cwd[strlen(client->cwd) - 1] == '/') {
            snprintf(client->cwd, sizeof(client->cwd), "%s%s", client->cwd, arg);
        } else {
            snprintf(client->cwd, sizeof(client->cwd), "%s/%s", client->cwd, arg);
        }
    }
    client->cwd[sizeof(client->cwd) - 1] = '\0';

    ftp_send_response(client->ctrl_sock, 250, "Directory successfully changed.");
    return 0;
}

static int ftp_cmd_type(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    if (arg[0] == 'I' || arg[0] == 'A') {
        ftp_send_response(client->ctrl_sock, 200, "Type set to I.");
        return 0;
    }

    ftp_send_response(client->ctrl_sock, 504, "Type not implemented.");
    return -1;
}

static int ftp_cmd_port(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    client->mode = FTP_MODE_ACTIVE;
    client->data_port = (uint16_t)(p1 * 256 + p2);

    ftp_send_response(client->ctrl_sock, 200, "PORT command successful.");
    return 0;
}

static int ftp_cmd_pasv(ftp_client_t *client, const char *arg)
{
    (void)arg;

    uint16_t pasv_port = tcp_ephemeral_alloc();
    tcp_socket_t *listener = tcp_listen(pasv_port);

    if (!listener) {
        ftp_send_response(client->ctrl_sock, 425, "Can't open data connection.");
        return -1;
    }

    client->pasv_listener = listener;
    client->pasv_port = pasv_port;
    client->mode = FTP_MODE_PASSIVE;

    net_interface_t *iface = net_get_default_interface();
    uint32_t ip = iface ? iface->ip.addr : 0x7F000001;

    char buf[128];
    snprintf(buf, sizeof(buf),
             "Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF,
             (pasv_port >> 8) & 0xFF, pasv_port & 0xFF);
    ftp_send_response(client->ctrl_sock, 227, buf);
    return 0;
}

static void *open_data_connection(ftp_client_t *client)
{
    net_interface_t *iface = net_get_default_interface();
    if (!iface)
        return NULL;

    if (client->mode == FTP_MODE_PASSIVE) {
        if (!client->pasv_listener)
            return NULL;
        tcp_socket_t *data_sock = tcp_accept((tcp_socket_t *)client->pasv_listener);
        return data_sock;
    } else {
        uint16_t src_port = tcp_ephemeral_alloc();
        ipv4_addr_t dst_ip;
        tcp_socket_t *ctrl = (tcp_socket_t *)client->ctrl_sock;
        dst_ip.addr = ctrl->remote_ip.addr;
        tcp_socket_t *data_sock = tcp_connect(iface, dst_ip,
                                               client->data_port, src_port);
        return data_sock;
    }
}

static void close_data_connection(ftp_client_t *client)
{
    if (client->data_sock) {
        tcp_close((tcp_socket_t *)client->data_sock);
        client->data_sock = NULL;
    }
    if (client->pasv_listener) {
        tcp_close((tcp_socket_t *)client->pasv_listener);
        client->pasv_listener = NULL;
    }
}

static int ftp_cmd_list(ftp_client_t *client, const char *arg)
{
    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg ? arg : ".", full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 150,
                      "Here comes the directory listing.");

    void *data_sock = open_data_connection(client);
    if (!data_sock) {
        ftp_send_response(client->ctrl_sock, 425,
                          "Can't open data connection.");
        return -1;
    }
    client->data_sock = data_sock;

    file_t *dir = NULL;
    if (vfs_opendir(full_path, &dir) == 0 && dir) {
        vfs_dirent_t entry;
        char line[512];

        while (vfs_readdir(dir, &entry) > 0) {
            int len = snprintf(line, sizeof(line),
                               "%c%-10s %3d %-8s %-8s %8ld %s %s\r\n",
                               (entry.type & FILE_MODE_DIR) ? 'd' : '-',
                               "rwxr-xr-x",
                               1, "user", "group",
                               (long)(entry.type & FILE_MODE_DIR ? 4096 : 0),
                               "Jan  1 00:00",
                               entry.name);
            if (len > 0) {
                tcp_send((tcp_socket_t *)data_sock, line, (uint32_t)len);
            }
        }
        vfs_closedir(dir);
    }

    close_data_connection(client);
    ftp_send_response(client->ctrl_sock, 226,
                      "Directory send OK.");
    return 0;
}

static int ftp_cmd_retr(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    file_t *file = NULL;
    if (vfs_open(full_path, FILE_MODE_READ, &file) < 0 || !file) {
        ftp_send_response(client->ctrl_sock, 550, "Failed to open file.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 150,
                      "Opening BINARY mode data connection.");

    void *data_sock = open_data_connection(client);
    if (!data_sock) {
        vfs_close(file);
        ftp_send_response(client->ctrl_sock, 425,
                          "Can't open data connection.");
        return -1;
    }
    client->data_sock = data_sock;

    uint8_t buf[4096];
    int32_t n;
    while ((n = vfs_read(file, buf, sizeof(buf))) > 0) {
        tcp_send((tcp_socket_t *)data_sock, buf, (uint32_t)n);
    }

    vfs_close(file);
    close_data_connection(client);
    ftp_send_response(client->ctrl_sock, 226, "Transfer complete.");
    return 0;
}

static int ftp_cmd_stor(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    file_t *file = NULL;
    if (vfs_creat(full_path, FILE_MODE_READ | FILE_MODE_WRITE) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "Failed to create file.");
        return -1;
    }
    if (vfs_open(full_path, FILE_MODE_WRITE, &file) < 0 || !file) {
        ftp_send_response(client->ctrl_sock, 550, "Failed to open file.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 150,
                      "Ok to send data.");

    void *data_sock = open_data_connection(client);
    if (!data_sock) {
        vfs_close(file);
        ftp_send_response(client->ctrl_sock, 425,
                          "Can't open data connection.");
        return -1;
    }
    client->data_sock = data_sock;

    uint8_t buf[4096];
    int n;
    while ((n = tcp_recv((tcp_socket_t *)data_sock, buf, sizeof(buf))) > 0) {
        vfs_write(file, buf, (uint32_t)n);
    }

    vfs_close(file);
    close_data_connection(client);
    ftp_send_response(client->ctrl_sock, 226, "Transfer complete.");
    return 0;
}

static int ftp_cmd_dele(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    if (vfs_unlink(full_path) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "Delete operation failed.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 250, "Delete operation successful.");
    return 0;
}

static int ftp_cmd_mkd(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    if (vfs_mkdir(full_path, 0755) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "Create directory operation failed.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 257, "Directory created.");
    return 0;
}

static int ftp_cmd_rmd(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    if (vfs_rmdir(full_path) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "Remove directory operation failed.");
        return -1;
    }

    ftp_send_response(client->ctrl_sock, 250, "Remove directory operation successful.");
    return 0;
}

static int ftp_cmd_noop(ftp_client_t *client, const char *arg)
{
    (void)arg;
    ftp_send_response(client->ctrl_sock, 200, "NOOP command successful.");
    return 0;
}

static int ftp_cmd_rnfr(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    char full_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, arg, full_path, sizeof(full_path));

    if (!path_is_allowed(client, full_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    inode_t stat;
    if (vfs_stat(full_path, &stat) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "File not found.");
        return -1;
    }

    strncpy(client->rename_from, arg, sizeof(client->rename_from) - 1);
    client->rename_from[sizeof(client->rename_from) - 1] = '\0';

    ftp_send_response(client->ctrl_sock, 350, "Ready for RNTO.");
    return 0;
}

static int ftp_cmd_rnto(ftp_client_t *client, const char *arg)
{
    if (!arg || !*arg) {
        ftp_send_response(client->ctrl_sock, 501, "Syntax error in parameters.");
        return -1;
    }

    if (client->rename_from[0] == '\0') {
        ftp_send_response(client->ctrl_sock, 503, "Need RNFR first.");
        return -1;
    }

    char from_path[FTP_MAX_PATH_LEN + 256];
    char to_path[FTP_MAX_PATH_LEN + 256];
    build_full_path(client, client->rename_from, from_path, sizeof(from_path));
    build_full_path(client, arg, to_path, sizeof(to_path));

    if (!path_is_allowed(client, from_path) ||
        !path_is_allowed(client, to_path)) {
        ftp_send_response(client->ctrl_sock, 550, "Permission denied.");
        return -1;
    }

    if (vfs_rename(from_path, to_path) < 0) {
        ftp_send_response(client->ctrl_sock, 550, "Rename failed.");
        return -1;
    }

    client->rename_from[0] = '\0';
    ftp_send_response(client->ctrl_sock, 250, "Rename successful.");
    return 0;
}

static void handle_ftp_command(ftp_client_t *client, char *cmd_line)
{
    char *cmd = cmd_line;
    char *arg = strchr(cmd_line, ' ');
    if (arg) {
        *arg = '\0';
        arg++;
        while (*arg == ' ') arg++;
    }

    char *crlf = strstr(cmd, "\r\n");
    if (crlf) *crlf = '\0';
    crlf = strchr(cmd, '\n');
    if (crlf) *crlf = '\0';

    for (char *p = cmd; *p; p++) {
        if (*p >= 'a' && *p <= 'z')
            *p = *p - 'a' + 'A';
    }

    if (strcmp(cmd, "USER") == 0) {
        ftp_cmd_user(client, arg);
    } else if (strcmp(cmd, "PASS") == 0) {
        ftp_cmd_pass(client, arg);
    } else if (strcmp(cmd, "QUIT") == 0) {
        ftp_cmd_quit(client, arg);
    } else if (strcmp(cmd, "SYST") == 0) {
        ftp_cmd_syst(client, arg);
    } else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
        ftp_cmd_pwd(client, arg);
    } else if (strcmp(cmd, "CWD") == 0 || strcmp(cmd, "XCWD") == 0) {
        ftp_cmd_cwd(client, arg);
    } else if (strcmp(cmd, "CDUP") == 0) {
        ftp_cmd_cwd(client, "..");
    } else if (strcmp(cmd, "TYPE") == 0) {
        ftp_cmd_type(client, arg);
    } else if (strcmp(cmd, "PORT") == 0) {
        ftp_cmd_port(client, arg);
    } else if (strcmp(cmd, "PASV") == 0) {
        ftp_cmd_pasv(client, arg);
    } else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
        ftp_cmd_list(client, arg);
    } else if (strcmp(cmd, "RETR") == 0) {
        ftp_cmd_retr(client, arg);
    } else if (strcmp(cmd, "STOR") == 0) {
        ftp_cmd_stor(client, arg);
    } else if (strcmp(cmd, "DELE") == 0) {
        ftp_cmd_dele(client, arg);
    } else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
        ftp_cmd_mkd(client, arg);
    } else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
        ftp_cmd_rmd(client, arg);
    } else if (strcmp(cmd, "RNFR") == 0) {
        ftp_cmd_rnfr(client, arg);
    } else if (strcmp(cmd, "RNTO") == 0) {
        ftp_cmd_rnto(client, arg);
    } else if (strcmp(cmd, "NOOP") == 0) {
        ftp_cmd_noop(client, arg);
    } else if (strcmp(cmd, "PASSIVE") == 0) {
        ftp_cmd_pasv(client, arg);
    } else {
        ftp_send_response(client->ctrl_sock, 502, "Command not implemented.");
    }
}

static void handle_ftp_client(ftp_server_t *server, tcp_socket_t *client_sock)
{
    ftp_client_t *client = (ftp_client_t *)kmalloc(sizeof(ftp_client_t));
    if (!client) {
        tcp_close(client_sock);
        return;
    }

    memset(client, 0, sizeof(ftp_client_t));
    client->ctrl_sock = client_sock;
    client->server = server;
    client->state = FTP_STATE_CONNECTED;
    client->mode = FTP_MODE_ACTIVE;
    strcpy(client->cwd, "/");

    ftp_send_response(client_sock, 220, server->welcome_msg);

    char *recv_buf = (char *)kmalloc(FTP_RECV_BUF_SIZE);
    if (!recv_buf) {
        kfree(client);
        tcp_close(client_sock);
        return;
    }

    uint32_t buf_len = 0;

    while (client->state != FTP_STATE_DISCONNECTED) {
        int n = tcp_recv(client_sock, recv_buf + buf_len,
                         FTP_RECV_BUF_SIZE - 1 - buf_len);
        if (n <= 0)
            break;

        buf_len += (uint32_t)n;
        recv_buf[buf_len] = '\0';

        char *line_start = recv_buf;
        char *line_end;

        while ((line_end = strstr(line_start, "\r\n")) != NULL ||
               (line_end = strchr(line_start, '\n')) != NULL) {
            *line_end = '\0';
            if (line_end > line_start && *(line_end - 1) == '\r')
                *(line_end - 1) = '\0';

            handle_ftp_command(client, line_start);

            if (client->state == FTP_STATE_DISCONNECTED)
                break;

            line_start = line_end + 1;
            if (*line_end == '\r' && *(line_end + 1) == '\n')
                line_start++;
        }

        uint32_t remaining = buf_len - (uint32_t)(line_start - recv_buf);
        if (remaining > 0 && line_start != recv_buf) {
            memmove(recv_buf, line_start, remaining);
        }
        buf_len = remaining;
    }

    close_data_connection(client);
    kfree(recv_buf);
    kfree(client);
    tcp_close(client_sock);
}

int ftp_server_start(ftp_server_t *server)
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

void ftp_server_stop(ftp_server_t *server)
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

void ftp_server_destroy(ftp_server_t *server)
{
    if (!server)
        return;

    if (server->running)
        ftp_server_stop(server);

    kfree(server);
}
