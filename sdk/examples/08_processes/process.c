/* process.c - Multi-process example
 * Demonstrates fork, exec, and inter-process communication.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 500, 350, "Process Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};

    funsos_draw_text(win, 20, 20, "Multi-Process Demo", blue);

    /* Get current PID */
    uint32_t pid = funsos_get_pid();
    char pid_text[64] = "Parent PID: ";

    funsos_draw_text(win, 20, 60, "Parent PID: (see console)", black);

    /* Create a pipe for IPC */
    int pipefd[2];
    funsos_pipe(pipefd);

    /* Fork a child process */
    int child = funsos_fork();

    if (child == 0) {
        /* Child process */
        funsos_file_close(pipefd[1]);  /* Close write end */

        char buf[128];
        int n = funsos_file_read(pipefd[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
        }

        funsos_file_close(pipefd[0]);
        funsos_exit(0);
    } else {
        /* Parent process */
        funsos_file_close(pipefd[0]);  /* Close read end */

        const char *msg = "Hello from parent!";
        funsos_file_write(pipefd[1], msg, 19);
        funsos_file_close(pipefd[1]);

        funsos_draw_text(win, 20, 90, "Forked child process", black);
        funsos_draw_text(win, 20, 120, "Sent message through pipe", black);

        /* Wait for child */
        int status;
        funsos_waitpid(child, &status);
        funsos_draw_text(win, 20, 150, "Child process exited", green);

        /* Spawn another process */
        int spawned = funsos_spawn("/usr/bin/ls", "/home");
        funsos_draw_text(win, 20, 180, "Spawned: /usr/bin/ls", black);
    }

    /* Memory info */
    uint32_t total, used;
    funsos_get_memory_info(&total, &used);
    funsos_draw_text(win, 20, 220, "Memory info available", black);

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
