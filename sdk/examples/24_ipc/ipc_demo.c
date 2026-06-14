/* ipc_demo.c - IPC (Inter-Process Communication) Demo
 * Demonstrates message queues, shared memory, semaphores, and pipes
 * using the funsos_ipc.h API.
 */

#include "funsos.h"

/* Helper: simple string length */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Helper: format number to string */
static void format_uint(uint32_t val, char *buf, int bufsize)
{
    int i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16];
    while (val > 0 && i < (int)sizeof(tmp) - 1) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int pos = 0;
    for (int k = i - 1; k >= 0 && pos < bufsize - 1; k--)
        buf[pos++] = tmp[k];
    buf[pos] = '\0';
}

int main(void)
{
    funsos_window_t win = funsos_create_window(60, 40, 700, 520, "IPC Demo - Message Queues, Shared Memory, Semaphores");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c  = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c  = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c  = FUNSOS_COLOR_ORANGE;
    funsos_color_t err_c   = FUNSOS_COLOR_RED;

    int line_y = 18;
    int lh = 22;
    char num_buf[32];

    /* Title */
    funsos_draw_text(win, 20, line_y, "=== IPC Mechanisms Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- 1. Message Queue Demo ---- */
    funsos_draw_text(win, 20, line_y, "[1] Message Queue (mq_create / mq_send / mq_recv)", label_c);
    line_y += lh;

    funsos_mq_t mq = funsos_mq_create("/demo_mq", 16, 4096);
    if (mq >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Message queue created: /demo_mq", good_c);
        line_y += lh;

        /* Send a message */
        funsos_ipc_msg_t msg;
        msg.type = 1;
        const char *payload = "Hello from IPC demo!";
        int payload_len = my_strlen(payload);
        msg.size = payload_len;
        for (int i = 0; i < payload_len && i < 4096; i++)
            msg.data[i] = (uint8_t)payload[i];
        msg.sender_pid = funsos_get_pid();
        msg.timestamp = funsos_get_time();

        int ret = funsos_mq_send(mq, &msg);
        if (ret >= 0) {
            funsos_draw_text(win, 40, line_y, "OK - Message sent to queue", good_c);
            line_y += lh;

            /* Receive the message back */
            funsos_ipc_msg_t recv_msg;
            ret = funsos_mq_try_recv(mq, &recv_msg, 1);
            if (ret >= 0) {
                funsos_draw_text(win, 40, line_y, "OK - Message received: \"", good_c);
                recv_msg.data[recv_msg.size] = '\0';
                funsos_draw_text(win, 240, line_y, (const char *)recv_msg.data, text_c);
                funsos_draw_text(win, 240 + recv_msg.size * 8, line_y, "\"", good_c);
            } else {
                funsos_draw_text(win, 40, line_y, "FAILED: Could not receive message", err_c);
            }
        } else {
            funsos_draw_text(win, 40, line_y, "FAILED: Could not send message", err_c);
        }

        funsos_mq_close(mq);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create message queue", err_c);
    }
    line_y += lh + 4;

    /* ---- 2. Shared Memory Demo ---- */
    funsos_draw_text(win, 20, line_y, "[2] Shared Memory (shm_create / shm_map / shm_unmap)", label_c);
    line_y += lh;

    funsos_shm_t shm = funsos_shm_create("/demo_shm", 1024);
    if (shm >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Shared memory created: /demo_shm (1024 bytes)", good_c);
        line_y += lh;

        void *shm_ptr = funsos_shm_map(shm, 1024);
        if (shm_ptr != NULL) {
            funsos_draw_text(win, 40, line_y, "OK - Shared memory mapped to process address space", good_c);
            line_y += lh;

            /* Write data to shared memory */
            const char *shm_data = "Shared memory data!";
            int shm_data_len = my_strlen(shm_data);
            for (int i = 0; i < shm_data_len && i < 1024; i++)
                ((char *)shm_ptr)[i] = shm_data[i];
            ((char *)shm_ptr)[shm_data_len] = '\0';

            funsos_draw_text(win, 40, line_y, "OK - Data written to shared memory: \"", good_c);
            funsos_draw_text(win, 320, line_y, (const char *)shm_ptr, text_c);
            funsos_draw_text(win, 320 + shm_data_len * 8, line_y, "\"", good_c);
            line_y += lh;

            /* Get size */
            uint32_t shm_size = funsos_shm_get_size(shm);
            format_uint(shm_size, num_buf, sizeof(num_buf));
            funsos_draw_text(win, 40, line_y, "  Shared memory size: ", label_c);
            funsos_draw_text(win, 190, line_y, num_buf, good_c);
            funsos_draw_text(win, 210, line_y, " bytes", label_c);

            funsos_shm_unmap(shm_ptr, 1024);
        } else {
            funsos_draw_text(win, 40, line_y, "FAILED: Could not map shared memory", err_c);
        }
        funsos_shm_close(shm);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create shared memory", err_c);
    }
    line_y += lh + 4;

    /* ---- 3. Semaphore Demo ---- */
    funsos_draw_text(win, 20, line_y, "[3] Semaphore (sem_create / sem_wait / sem_post)", label_c);
    line_y += lh;

    funsos_sem_t sem = funsos_sem_create("/demo_sem", 1);
    if (sem >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Semaphore created: /demo_sem (initial=1)", good_c);
        line_y += lh;

        /* Try to acquire */
        int ret = funsos_sem_try_wait(sem);
        if (ret >= 0) {
            funsos_draw_text(win, 40, line_y, "OK - Semaphore acquired (P operation)", good_c);
            line_y += lh;

            int val = funsos_sem_get_value(sem);
            format_uint((uint32_t)val, num_buf, sizeof(num_buf));
            funsos_draw_text(win, 40, line_y, "  Semaphore value after wait: ", label_c);
            funsos_draw_text(win, 240, line_y, num_buf, text_c);
            line_y += lh;

            /* Release */
            funsos_sem_post(sem);
            funsos_draw_text(win, 40, line_y, "OK - Semaphore released (V operation)", good_c);
        } else {
            funsos_draw_text(win, 40, line_y, "FAILED: Could not acquire semaphore", err_c);
        }
        funsos_sem_close(sem);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create semaphore", err_c);
    }
    line_y += lh + 4;

    /* ---- 4. Pipe Demo ---- */
    funsos_draw_text(win, 20, line_y, "[4] Pipe (pipe_create / pipe_read / pipe_write)", label_c);
    line_y += lh;

    int pipefd[2];
    int ret = funsos_pipe(pipefd);
    if (ret >= 0) {
        funsos_draw_text(win, 40, line_y, "OK - Anonymous pipe created (fd[0]=read, fd[1]=write)", good_c);
        line_y += lh;

        const char *pipe_data = "Pipe message!";
        int pipe_data_len = my_strlen(pipe_data);
        int written = funsos_pipe_write(pipefd[1], pipe_data, pipe_data_len);
        if (written > 0) {
            format_uint((uint32_t)written, num_buf, sizeof(num_buf));
            funsos_draw_text(win, 40, line_y, "OK - Wrote ", good_c);
            funsos_draw_text(win, 120, line_y, num_buf, good_c);
            funsos_draw_text(win, 135, line_y, " bytes to pipe", good_c);
            line_y += lh;

            char read_buf[128];
            int n = funsos_pipe_read(pipefd[0], read_buf, sizeof(read_buf) - 1);
            if (n > 0) {
                read_buf[n] = '\0';
                funsos_draw_text(win, 40, line_y, "OK - Read from pipe: \"", good_c);
                funsos_draw_text(win, 200, line_y, read_buf, text_c);
                funsos_draw_text(win, 200 + n * 8, line_y, "\"", good_c);
            } else {
                funsos_draw_text(win, 40, line_y, "FAILED: Could not read from pipe", err_c);
            }
        } else {
            funsos_draw_text(win, 40, line_y, "FAILED: Could not write to pipe", err_c);
        }

        funsos_file_close(pipefd[0]);
        funsos_file_close(pipefd[1]);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create pipe", err_c);
    }
    line_y += lh + 4;

    /* ---- 5. Mutex Demo ---- */
    funsos_draw_text(win, 20, line_y, "[5] Mutex (mutex_create / mutex_lock / mutex_unlock)", label_c);
    line_y += lh;

    /* Mutexes are implemented as binary semaphores */
    funsos_sem_t mutex = funsos_sem_create("/demo_mutex", 1);
    if (mutex >= 0) {
        int ret = funsos_sem_wait(mutex);
        if (ret >= 0) {
            funsos_draw_text(win, 40, line_y, "OK - Mutex locked (critical section entered)", good_c);
            line_y += lh;

            funsos_draw_text(win, 40, line_y, "  ... performing thread-safe operation ...", label_c);
            line_y += lh;

            funsos_sem_post(mutex);
            funsos_draw_text(win, 40, line_y, "OK - Mutex unlocked (critical section exited)", good_c);
        }
        funsos_sem_close(mutex);
    } else {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create mutex", err_c);
    }
    line_y += lh + 8;

    /* ---- Summary ---- */
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_text(win, 20, line_y, "[Summary] IPC Mechanisms Demonstrated:", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 40, line_y, "  [v] Message Queues (mq_create, mq_send, mq_recv)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Shared Memory (shm_create, shm_map, shm_unmap)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Semaphores (sem_create, sem_wait, sem_post)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Pipes (pipe, pipe_read, pipe_write)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Mutexes (binary semaphore based)", good_c);
    line_y += lh + 8;

    /* Footer */
    funsos_draw_line(win, 15, 508, 685, 508, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 516, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}