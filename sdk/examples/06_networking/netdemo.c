/* netdemo.c - Networking example
 * Demonstrates TCP socket connections and HTTP requests.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 60, 600, 400, "Network Demo");
    funsos_fill_window(win, 0x0C0C0C);

    funsos_color_t green = {0x00, 0xFF, 0x00, 0xFF};
    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS Network Demo", green);
    funsos_draw_text(win, 20, 50, "Initializing network...", white);

    /* Create a TCP socket */
    int sock = funsos_socket(0, 1, 0);  /* AF_INET, SOCK_STREAM */
    if (sock < 0) {
        funsos_draw_text(win, 20, 80, "Failed to create socket", white);
    } else {
        funsos_draw_text(win, 20, 80, "Socket created successfully", green);

        /* Connect to a server */
        funsos_sockaddr_in_t addr;
        funsos_ipv4_t ip = funsos_inet_addr("93.184.216.34");
        addr.addr = ip;
        addr.port = funsos_htons(80);

        funsos_draw_text(win, 20, 110, "Connecting to 93.184.216.34:80...", white);

        if (funsos_connect(sock, &addr) == 0) {
            funsos_draw_text(win, 20, 140, "Connected!", green);

            /* Send HTTP request */
            const char *request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
            funsos_send(sock, request, 40, 0);

            /* Receive response */
            char buffer[1024];
            int received = funsos_recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (received > 0) {
                buffer[received < 1023 ? received : 1023] = '\0';
                funsos_draw_text(win, 20, 170, "Response received:", green);
                funsos_draw_text(win, 20, 190, buffer, white);
            }

            funsos_closesocket(sock);
        } else {
            funsos_draw_text(win, 20, 140, "Connection failed", white);
        }
    }

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
