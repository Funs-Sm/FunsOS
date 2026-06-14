/* ipc_demo.c - Inter-Process Communication Demo
 * Demonstrates shared memory, message queues, and pipes
 * between parent and child processes using IPC mechanisms.
 * The parent process forks a child, then they communicate
 * through multiple IPC channels simultaneously.
 */

#include "funsos.h"

/* ---- Shared memory region structure ---- */
#define SHM_SIZE 256

typedef struct {
    char message[SHM_SIZE];  /* Shared message buffer */
    uint32_t counter;        /* Shared atomic counter */
    uint32_t ready_flag;     /* Synchronization flag */
} ipc_shared_data_t;

/* ---- Message queue entry structure ---- */
#define MSG_MAX_LEN 128
#define MQ_CAPACITY 8

typedef struct {
    char data[MSG_MAX_LEN];  /* Message payload */
    uint32_t sender_pid;     /* Sender process ID */
    uint32_t msg_type;       /* Message type identifier */
} ipc_message_t;

/* ---- Demo constants ---- */
#define NUM_CHILD_MESSAGES 5
#define PARENT_MSG "Hello child! This is your parent speaking."
#define CHILD_MSG  "Hello parent! Child process acknowledges."

/* Helper: simple string length calculation */
static int my_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Helper: copy string safely */
static void my_strcpy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Simulated shared memory allocation (uses mmap under the hood) */
static ipc_shared_data_t *ipc_create_shm(void)
{
    return (ipc_shared_data_t *)funsos_mmap(NULL, sizeof(ipc_shared_data_t),
                                             FUNSOS_PROT_READ | FUNSOS_PROT_WRITE,
                                             FUNSOS_MAP_SHARED | FUNSOS_MAP_ANONYMOUS);
}

static int ipc_destroy_shm(ipc_shared_data_t *shm)
{
    return funsos_munmap(shm, sizeof(ipc_shared_data_t));
}

/* ---- Main entry point ---- */
int main(void)
{
    /* Create display window */
    funsos_window_t win = funsos_create_window(60, 40, 700, 480, "IPC Demo - Inter-Process Communication");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    funsos_color_t title_c  = FUNSOS_COLOR_BLUE;
    funsos_color_t text_c   = FUNSOS_COLOR_BLACK;
    funsos_color_t label_c  = FUNSOS_COLOR_DARK_GRAY;
    funsos_color_t good_c   = FUNSOS_COLOR_GREEN;
    funsos_color_t warn_c   = FUNSOS_COLOR_ORANGE;
    funsos_color_t err_c    = FUNSOS_COLOR_RED;

    int line_y = 18;
    int lh = 22;

    /* ---- Title ---- */
    funsos_draw_text(win, 20, line_y, "=== Inter-Process Communication Demo ===", title_c);
    line_y += lh + 4;
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 10;

    /* ---- Step 1: Create shared memory segment ---- */
    funsos_draw_text(win, 20, line_y, "[Step 1] Creating shared memory segment...", label_c);
    line_y += lh;

    ipc_shared_data_t *shm = ipc_create_shm();
    if (shm == NULL) {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create shared memory!", err_c);
        goto cleanup;
    }

    /* Initialize shared memory */
    for (int i = 0; i < SHM_SIZE; i++) shm->message[i] = 0;
    shm->counter = 0;
    shm->ready_flag = 0;

    funsos_draw_text(win, 40, line_y, "OK - Shared memory allocated successfully", good_c);
    line_y += lh;

    /* ---- Step 2: Create pipe for bidirectional communication ---- */
    funsos_draw_text(win, 20, line_y, "[Step 2] Creating pipe channel...", label_c);
    line_y += lh;

    int pipe_parent_to_child[2];  /* Parent writes, child reads */
    int ret = funsos_pipe(pipe_parent_to_child);
    if (ret != 0) {
        funsos_draw_text(win, 40, line_y, "FAILED: Could not create pipe!", err_c);
        goto cleanup;
    }
    funsos_draw_text(win, 40, line_y, "OK - Pipe created (fd read/write)", good_c);
    line_y += lh;

    /* ---- Step 3: Fork child process ---- */
    funsos_draw_text(win, 20, line_y, "[Step 3] Forking child process...", label_c);
    line_y += lh;

    uint32_t parent_pid = funsos_get_pid();
    int child_pid = funsos_fork();

    if (child_pid < 0) {
        funsos_draw_text(win, 40, line_y, "FAILED: fork() returned error!", err_c);
        goto cleanup;
    }

    if (child_pid == 0) {
        /* ====== CHILD PROCESS ====== */
        funsos_file_close(pipe_parent_to_child[1]);  /* Close write end of pipe */

        uint32_t child_pid_val = funsos_get_pid();

        /* Read message from parent via pipe */
        char pipe_buf[MSG_MAX_LEN];
        int n = funsos_file_read(pipe_parent_to_child[0], pipe_buf, sizeof(pipe_buf) - 1);
        if (n > 0) {
            pipe_buf[n] = '\0';
            /* Write received message into shared memory */
            my_strcpy(shm->message, pipe_buf, SHM_SIZE);
            shm->sender_pid = child_pid_val;  /* Store who processed it */
            shm->ready_flag = 1;              /* Signal parent that we're done */
            shm->counter++;                    /* Increment shared counter */
        }

        funsos_file_close(pipe_parent_to_child[0]);
        funsos_exit(0);
    }

    /* ====== PARENT PROCESS ====== */
    funsos_file_close(pipe_parent_to_child[0]);  /* Close read end of pipe */

    funsos_draw_text(win, 40, line_y, "OK - Child process created", good_c);
    line_y += lh;

    /* Display PID information */
    char pid_buf[32];
    /* Format parent PID */
    {
        uint32_t val = parent_pid;
        int pi = 0;
        char tmp[16];
        if (val == 0) { tmp[pi++] = '0'; }
        else { char rev[16]; int ri = 0; while (val > 0) { rev[ri++] = '0' + (val%10); val/=10; } for (int k=ri-1;k>=0;k--) tmp[pi++]=rev[k]; }
        tmp[pi] = '\0';
        my_strcpy(pid_buf, tmp, sizeof(pid_buf));
    }
    funsos_draw_text(win, 40, line_y, "Parent PID: ", label_c);
    funsos_draw_text(win, 130, line_y, pid_buf, text_c);

    /* Format child PID */
    {
        uint32_t val = (uint32_t)child_pid;
        int pi = 0;
        char tmp[16];
        if (val == 0) { tmp[pi++] = '0'; }
        else { char rev[16]; int ri = 0; while (val > 0) { rev[ri++] = '0' + (val%10); val/=10; } for (int k=ri-1;k>=0;k--) tmp[pi++]=rev[k]; }
        tmp[pi] = '\0';
        my_strcpy(pid_buf, tmp, sizeof(pid_buf));
    }
    funsos_draw_text(win, 230, line_y, "Child PID: ", label_c);
    funsos_draw_text(win, 310, line_y, pid_buf, text_c);
    line_y += lh + 4;

    /* ---- Step 4: Send message to child via pipe ---- */
    funsos_draw_text(win, 20, line_y, "[Step 4] Sending message to child via pipe...", label_c);
    line_y += lh;

    const char *parent_msg = PARENT_MSG;
    int msg_len = my_strlen(parent_msg);
    int sent = funsos_file_write(pipe_parent_to_child[1], parent_msg, msg_len);
    funsos_file_close(pipe_parent_to_child[1]);  /* Done writing */

    if (sent > 0) {
        funsos_draw_text(win, 40, line_y, "Sent: \"", label_c);
        funsos_draw_text(win, 85, line_y, parent_msg, warn_c);
        funsos_draw_text(win, 85 + msg_len * 8, line_y, "\"", label_c);
    } else {
        funsos_draw_text(win, 40, line_y, "WARNING: Pipe write returned error", warn_c);
    }
    line_y += lh;

    /* ---- Step 5: Wait for child to finish ---- */
    funsos_draw_text(win, 20, line_y, "[Step 5] Waiting for child process...", label_c);
    line_y += lh;

    int child_status = 0;
    int waited = funsos_waitpid(child_pid, &child_status);
    if (waited > 0) {
        funsos_draw_text(win, 40, line_y, "OK - Child process exited normally", good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "WARNING: waitpid() issue detected", warn_c);
    }
    line_y += lh + 4;

    /* ---- Step 6: Read results from shared memory ---- */
    funsos_draw_text(win, 20, line_y, "[Step 6] Reading results from shared memory...", label_c);
    line_y += lh;

    if (shm->ready_flag) {
        funsos_draw_text(win, 40, line_y, "Shared memory content:", label_c);
        line_y += lh;
        funsos_draw_text(win, 50, line_y, "Message: \"", label_c);
        funsos_draw_text(win, 125, line_y, shm->message, text_c);
        funsos_draw_text(win, 125 + my_strlen(shm->message) * 8, line_y, "\"", label_c);
        line_y += lh;

        /* Show counter value */
        char cnt_buf[16];
        uint32_t cval = shm->counter;
        int ci = 0;
        if (cval == 0) cnt_buf[ci++] = '0';
        else { char rev[8]; int ri = 0; while(cval>0){rev[ri++]='0'+(cval%10);cval/=10;}for(int k=ri-1;k>=0;k--)cnt_buf[ci++]=rev[k]; }
        cnt_buf[ci]='\0';
        funsos_draw_text(win, 50, line_y, "Counter: ", label_c);
        funsos_draw_text(win, 115, line_y, cnt_buf, good_c);
    } else {
        funsos_draw_text(win, 40, line_y, "WARNING: Shared memory not updated by child", warn_c);
    }
    line_y += lh + 8;

    /* ---- Summary section ---- */
    funsos_draw_line(win, 15, line_y, 685, line_y, FUNSOS_COLOR_GRAY);
    line_y += 8;
    funsos_draw_rect(win, 15, line_y - 2, 670, lh + 4, FUNSOS_COLOR_LIGHT_GRAY);
    funsos_draw_text(win, 20, line_y, "[Summary] IPC Mechanisms Demonstrated:", title_c);
    line_y += lh + 4;

    funsos_draw_text(win, 40, line_y, "  [v] Shared Memory (mmap-based, shared data between processes)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Anonymous Pipe (unidirectional parent-to-child channel)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Process Fork (child inherits parent's resources)", good_c);
    line_y += lh;
    funsos_draw_text(win, 40, line_y, "  [v] Wait/PID (parent waits for child termination)", good_c);
    line_y += lh + 8;

    /* Cleanup shared memory */
cleanup:
    if (shm != NULL) {
        ipc_destroy_shm(shm);
    }

    /* Footer */
    funsos_draw_line(win, 15, 468, 685, 468, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 20, 476, "Press ESC to exit", FUNSOS_COLOR_DARK_GRAY);

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
