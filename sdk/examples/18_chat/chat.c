/* chat.c - Chat application example
 * A simple network chat client.
 */

#include "funsos.h"

#define MAX_MESSAGES 20
#define MSG_LEN 128

static char messages[MAX_MESSAGES][MSG_LEN];
static int msg_count = 0;
static char input_buf[MSG_LEN];
static int input_len = 0;

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 50, 500, 400, "Chat");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};
    funsos_color_t gray  = {0xF0, 0xF0, 0xF0, 0xFF};

    /* Welcome message */
    messages[0][0] = 'W'; messages[0][1] = 'e'; messages[0][2] = 'l';
    messages[0][3] = 'c'; messages[0][4] = 'o'; messages[0][5] = 'm';
    messages[0][6] = 'e'; messages[0][7] = '!'; messages[0][8] = '\0';
    msg_count = 1;

    /* Try to connect to chat server */
    int sock = funsos_socket(0, 1, 0);
    int connected = 0;

    if (sock >= 0) {
        funsos_sockaddr_in_t addr;
        funsos_ipv4_t ip = funsos_inet_addr("127.0.0.1");
        addr.addr = ip;
        addr.port = funsos_htons(8080);

        if (funsos_connect(sock, &addr) == 0) {
            connected = 1;
        }
    }

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B) break;

            if (event.key == 0x0D) {  /* Enter */
                if (input_len > 0 && msg_count < MAX_MESSAGES) {
                    /* Copy input to messages */
                    for (int i = 0; i < input_len && i < MSG_LEN - 1; i++)
                        messages[msg_count][i] = input_buf[i];
                    messages[msg_count][input_len] = '\0';
                    msg_count++;

                    /* Send to server if connected */
                    if (connected) {
                        funsos_send(sock, input_buf, input_len, 0);
                    }

                    input_len = 0;
                    input_buf[0] = '\0';
                }
            } else if (event.key == 0x08) {  /* Backspace */
                if (input_len > 0) input_len--;
                input_buf[input_len] = '\0';
            } else if (event.key >= 0x20 && event.key < 0x7F) {
                if (input_len < MSG_LEN - 1) {
                    input_buf[input_len++] = (char)event.key;
                    input_buf[input_len] = '\0';
                }
            }
        }

        /* Render */
        funsos_fill_window(win, 0xFFFFFF);

        /* Title bar */
        funsos_draw_rect(win, 0, 0, 500, 30, blue);
        funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
        funsos_draw_text(win, 10, 8, "FUNSOS Chat", white);

        /* Messages area */
        for (int i = 0; i < msg_count; i++) {
            int y = 40 + i * 18;
            funsos_draw_text(win, 10, y, messages[i], black);
        }

        /* Input area */
        funsos_draw_rect(win, 0, 370, 500, 30, gray);
        funsos_draw_text(win, 10, 376, "> ", black);
        funsos_draw_text(win, 30, 376, input_buf, black);

        /* Connection status */
        if (connected) {
            funsos_draw_text(win, 400, 376, "Online", white);
        }
    }

    if (connected) funsos_closesocket(sock);
    funsos_destroy_window(win);
    return 0;
}
