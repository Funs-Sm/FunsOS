/* net_server.c - Network Server Demo
 * A simple TCP echo server that listens on port 8080.
 * Handles multiple client connections using select() for
 * non-blocking I/O multiplexing. Demonstrates socket API
 * usage from funsos_network.h.
 *
 * Architecture:
 *   Server binds to 0.0.0.0:8080, accepts incoming connections,
 *   echoes back any data received from clients, and maintains
 *   a connection log displayed in the GUI window.
 */

#include "funsos.h"

/* ---- Server Configuration ---- */
#define SERVER_PORT         8080
#define MAX_CLIENTS         8
#define RECV_BUFFER_SIZE    1024
#define LISTEN_BACKLOG      5

/* ---- Client connection tracking ---- */
typedef struct {
    int           fd;             /* Socket file descriptor */
    int           active;         /* Whether this slot is in use */
    uint32_t      ip_addr;        /* Client IP address (network byte order) */
    uint16_t      port;           /* Client port */
    uint32_t      bytes_recv;     /* Total bytes received from this client */
    uint32_t      bytes_sent;     /* Total bytes sent to this client */
    uint32_t      connect_time;   /* Tick when connected */
} client_conn_t;

/* Global state */
static client_conn_t clients[MAX_CLIENTS];
static int server_fd = -1;
static uint32_t total_connections = 0;
static uint32_t total_bytes_echoed = 0;

/* ---- Helper: format IP address from network byte order to string ---- */
static void format_ip(uint32_t addr_net, char *buf, int buflen)
{
    uint8_t a = (addr_net >> 24) & 0xFF;
    uint8_t b = (addr_net >> 16) & 0xFF;
    uint8_t c = (addr_net >> 8) & 0xFF;
    uint8_t d = addr_net & 0xFF;

    int i = 0;
    /* First octet */
    if (a >= 100) { buf[i++] = '0' + a/100; a %= 100; }
    if (i > 0 || a >= 10) { buf[i++] = '0' + a/10; a %= 10; }
    buf[i++] = '0' + a; buf[i++] = '.';
    /* Second octet */
    if (b >= 100) { buf[i++] = '0' + b/100; b %= 100; }
    if (i > 4 || b >= 10) { buf[i++] = '0' + b/10; b %= 10; }
    buf[i++] = '0' + b; buf[i++] = '.';
    /* Third octet */
    if (c >= 100) { buf[i++] = '0' + c/100; c %= 100; }
    if (i > 9 || c >= 10) { buf[i++] = '0' + c/10; c %= 10; }
    buf[i++] = '0' + c; buf[i++] = '.';
    /* Fourth octet */
    if (d >= 100) { buf[i++] = '0' + d/100; d %= 100; }
    if (i > 14 || d >= 10) { buf[i++] = '0' + d/10; d %= 10; }
    buf[i++] = '0' + d;
    buf[i] = '\0';
}

/* ---- Helper: format integer to decimal string ---- */
static void fmt_uint(uint32_t val, char *buf, int buflen)
{
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int ti = 0;
        while (val > 0 && ti < 15) { tmp[ti++] = '0' + (val % 10); val /= 10; }
        for (int k = ti - 1; k >= 0 && i < buflen - 1; k--) buf[i++] = tmp[k];
    }
    buf[i] = '\0';
}

/* Find a free client slot, returns index or -1 if full */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) return i;
    }
    return -1;
}

/* Initialize all client slots to empty */
static void init_clients(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].active = 0;
        clients[i].ip_addr = 0;
        clients[i].port = 0;
        clients[i].bytes_recv = 0;
        clients[i].bytes_sent = 0;
        clients[i].connect_time = 0;
    }
}

/* Accept a new client connection */
static int accept_new_client(void)
{
    funsos_sockaddr_in_t client_addr;
    int client_fd = funsos_accept(server_fd, &client_addr);

    if (client_fd < 0) return -1;

    int slot = find_free_slot();
    if (slot < 0) {
        /* Server full - reject */
        funsos_closesocket(client_fd);
        return -1;
    }

    /* Record client info */
    clients[slot].fd = client_fd;
    clients[slot].active = 1;
    clients[slot].ip_addr = client_addr.sin_addr.addr;
    clients[slot].port = client_addr.sin_port;
    clients[slot].bytes_recv = 0;
    clients[slot].bytes_sent = 0;
    clients[slot].connect_time = funsos_get_ticks();

    total_connections++;
    return slot;
}

/* Handle data from an existing client (echo it back) */
static void handle_client_data(int slot)
{
    char buf[RECV_BUFFER_SIZE];
    int n = funsos_recv(clients[slot].fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        /* Error or connection closed - clean up */
        funsos_closesocket(clients[slot].fd);
        clients[slot].fd = -1;
        clients[slot].active = 0;
        return;
    }

    buf[n] = '\0';  /* Null-terminate for safety */

    /* Echo data back to client */
    int sent = funsos_send(clients[slot].fd, buf, n, 0);
    (void)sent;  /* May send less than n, that's OK for echo */

    /* Update statistics */
    clients[slot].bytes_recv += (uint32_t)n;
    clients[slot].bytes_sent += (uint32_t)(sent > 0 ? sent : 0);
    total_bytes_echoed += (uint32_t)n;
}

/* ---- Render server status to the window ---- */
static void render_status(funsos_window_t win)
{
    static uint32_t last_render_tick = 0;
    uint32_t now = funsos_get_ticks();

    /* Only re-render periodically to avoid flickering */
    if (now - last_render_tick < 100)  /* ~100 tick minimum between renders */
        return;
    last_render_tick = now;

    /* Clear and redraw status panel */
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t ct = FUNSOS_COLOR_BLUE;
    funsos_color_t cl = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t cv = FUNSOS_COLOR_BLACK;
    funsos_color_t cg = FUNSOS_COLOR_GREEN;
    funsos_color_t co = FUNSOS_COLOR_ORANGE;
    funsos_color_t cr = FUNSOS_COLOR_RED;

    int y = 14;
    int lh = 20;

    /* ---- Header ---- */
    funsos_draw_text(win, 16, y, "=== TCP Echo Server (Port 8080) ===", ct);
    y += lh + 2;
    funsos_draw_line(win, 12, y, 628, y, FUNSOS_COLOR_GRAY);
    y += 6;

    /* ---- Server status ---- */
    char tmp[32];

    funsos_draw_text(win, 16, y, "Status: ", cl);
    if (server_fd >= 0)
        funsos_draw_text(win, 74, y, "LISTENING", cg);
    else
        funsos_draw_text(win, 74, y, "STOPPED", cr);
    y += lh;

    funsos_draw_text(win, 16, y, "Port:   ", cl);
    funsos_draw_text(win, 74, y, "8080", cv);
    y += lh;

    fmt_uint(total_connections, tmp, sizeof(tmp));
    funsos_draw_text(win, 16, y, "Conns:  ", cl);
    funsos_draw_text(win, 74, y, tmp, cv);
    y += lh;

    fmt_uint(total_bytes_echoed, tmp, sizeof(tmp));
    funsos_draw_text(win, 16, y, "Echoed: ", cl);
    funsos_draw_text(win, 74, y, tmp, cv);
    funsos_draw_text(win, 74 + my_strlen(tmp) * 8, y, " bytes", cl);
    y += lh + 4;

    /* ---- Connected clients table ---- */
    funsos_draw_rect(win, 12, y - 2, 628, lh + 2, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 16, y, "--- Active Clients ---", ct);
    y += lh + 4;

    int active_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) continue;
        active_count++;

        char ip_str[20];
        format_ip(clients[i].ip_addr, ip_str, sizeof(ip_str));

        /* Client line: "#N IP:PORT recv=X sent=Y" */
        char prefix[8];
        prefix[0] = '#'; prefix[1] = '0' + (i + 1) / 10; prefix[2] = '0' + (i + 1) % 10;
        prefix[3] = ' '; prefix[4] = '\0';

        funsos_draw_text(win, 20, y, prefix, co);
        funsos_draw_text(win, 52, y, ip_str, cv);

        fmt_uint(clients[i].bytes_recv, tmp, sizeof(tmp));
        funsos_draw_text(win, 170, y, "rx=", cl);
        funsos_draw_text(win, 190, y, tmp, cg);

        fmt_uint(clients[i].bytes_sent, tmp, sizeof(tmp));
        funsos_draw_text(win, 250, y, "tx=", cl);
        funsos_draw_text(win, 270, y, tmp, cg);

        y += lh;

        if (y > 420) {  /* Don't overflow window */
            funsos_draw_text(win, 20, y, "... (more clients)", cl);
            break;
        }
    }

    if (active_count == 0) {
        funsos_draw_text(win, 20, y, "(no active connections)", cl);
        y += lh;
    }

    y += 8;

    /* ---- Legend / instructions ---- */
    funsos_draw_line(win, 12, y, 628, y, FUNSOS_COLOR_GRAY);
    y += 4;
    funsos_draw_text(win, 16, y, "ESC=Quit | Server runs until exit", cl);
    y += lh;
    funsos_draw_text(win, 16, y, "Connect with: telnet localhost 8080", cl);
}

int main(void)
{
    /* Create status display window */
    funsos_window_t win = funsos_create_window(100, 50, 640, 480, "Network Server Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* ---- Step 1: Create TCP socket ---- */
    server_fd = funsos_socket(FUNSOS_AF_INET, FUNSOS_SOCK_STREAM, FUNSOS_IPPROTO_TCP);
    if (server_fd < 0) {
        funsos_draw_text(win, 20, 20, "FATAL: Cannot create socket!", FUNSOS_COLOR_RED);
        goto error_exit;
    }

    /* Allow address reuse so we can restart quickly */
    int reuse = 1;
    funsos_setsockopt(server_fd, FUNSOS_SOL_SOCKET, FUNSOS_SO_REUSEADDR, &reuse, sizeof(reuse));

    /* ---- Step 2: Bind to port 8080 ---- */
    funsos_sockaddr_in_t server_addr;
    server_addr.sin_family = FUNSOS_AF_INET;
    server_addr.sin_port = funsos_htons(SERVER_PORT);
    server_addr.sin_addr = funsos_inet_addr("0.0.0.0");

    if (funsos_bind(server_fd, &server_addr) < 0) {
        funsos_draw_text(win, 20, 20, "FATAL: Bind failed (port in use?)", FUNSOS_COLOR_RED);
        goto error_exit;
    }

    /* ---- Step 3: Start listening ---- */
    if (funsos_listen(server_fd, LISTEN_BACKLOG) < 0) {
        funsos_draw_text(win, 20, 20, "FATAL: Listen failed!", FUNSOS_COLOR_RED);
        goto error_exit;
    }

    /* Initialize client tracking */
    init_clients();

    /* ---- Main server loop ---- */
    funsos_event_t event;
    int running = 1;

    while (running) {
        /* Check for GUI events (non-blocking poll) */
        while (funsos_poll_event(&event)) {
            if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B) {
                running = 0;
                break;
            }
            if (event.type == FUNSOS_EVENT_WINDOW_CLOSE) {
                running = 0;
                break;
            }
        }
        if (!running) break;

        /* Use select() to check for activity on server socket and all client sockets */
        /* Timeout = 0 for non-blocking check (poll mode) */
        int activity = funsos_select(server_fd + 1, NULL, NULL, NULL, 0);

        if (activity > 0) {
            /* Check if there's a new incoming connection */
            /* (In a real implementation we'd build fd_sets for select) */
            accept_new_client();
        }

        /* Service each active client */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                handle_client_data(i);
            }
        }

        /* Update the status display */
        render_status(win);

        /* Brief yield to prevent busy-spinning */
        funsos_yield();
    }

    /* ---- Cleanup: close all client connections ---- */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            funsos_closesocket(clients[i].fd);
            clients[i].active = 0;
        }
    }

    /* Close server socket */
    if (server_fd >= 0) {
        funsos_shutdown(server_fd, FUNSOS_SHUT_RDWR);
        funsos_closesocket(server_fd);
        server_fd = -1;
    }

    /* Final status display */
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);
    funsos_draw_text(win, 20, 20, "Server Shutdown Complete", FUNSOS_COLOR_BLUE);
    funsos_draw_text(win, 20, 50, "All connections closed.", FUNSOS_COLOR_DARK_GRAY);

    char tmp[32];
    fmt_uint(total_connections, tmp, sizeof(tmp));
    funsos_draw_text(win, 20, 80, "Total connections: ", FUNSOS_COLOR_DARK_GRAY);
    funsos_draw_text(win, 180, 80, tmp, FUNSOS_COLOR_BLACK);

    fmt_uint(total_bytes_echoed, tmp, sizeof(tmp));
    funsos_draw_text(win, 20, 110, "Total bytes echoed: ", FUNSOS_COLOR_DARK_GRAY);
    funsos_draw_text(win, 195, 110, tmp, FUNSOS_COLOR_BLACK);
    funsos_draw_text(win, 195 + my_strlen(tmp) * 8, 110, " bytes", FUNSOS_COLOR_DARK_GRAY);

    funsos_draw_text(win, 20, 160, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

    while (1) {
        if (funsos_wait_event(&event))
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;

error_exit:
    funsos_draw_text(win, 20, 50, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);
    while (1) {
        if (funsos_wait_event(&event))
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }
    funsos_destroy_window(win);
    return 1;
}
