/* fileio.c - File I/O example
 * Demonstrates reading and writing files on FUNSOS.
 */

#include "funsos.h"

int main(void)
{
    /* Write a file */
    int fd = funsos_file_open("/tmp/test.txt", 2);  /* Read/Write */
    if (fd < 0) {
        /* Try creating the file */
        fd = funsos_file_open("/tmp/test.txt", 3);  /* Create/Write */
    }

    if (fd >= 0) {
        const char *message = "Hello from FUNSOS File I/O!\nThis is a test file.\n";
        funsos_file_write(fd, message, 48);
        funsos_file_close(fd);
    }

    /* Read the file back */
    fd = funsos_file_open("/tmp/test.txt", 0);  /* Read only */
    if (fd >= 0) {
        char buffer[256];
        int bytes = funsos_file_read(fd, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
        }
        funsos_file_close(fd);
    }

    /* Directory operations */
    funsos_file_mkdir("/tmp/demo_dir");
    funsos_file_chdir("/tmp");

    char cwd[256];
    funsos_file_getcwd(cwd, sizeof(cwd));

    /* Display results in a window */
    funsos_window_t win = funsos_create_window(100, 80, 500, 350, "File I/O Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_draw_text(win, 20, 20, "File I/O Demo", blue);
    funsos_draw_text(win, 20, 50, "File written to: /tmp/test.txt", black);
    funsos_draw_text(win, 20, 70, "Current directory: /tmp", black);
    funsos_draw_text(win, 20, 90, "Created directory: /tmp/demo_dir", black);

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
