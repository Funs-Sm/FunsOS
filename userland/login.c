#include "user_syscall.h"
#include "string.h"

static char username[32];
static char password[32];

int main(void) {
    while (1) {
        sys_write(1, "Login: ", 7);
        int u_len = sys_read(0, username, 31);
        if (u_len <= 0) continue;
        username[u_len] = '\0';
        if (u_len > 0 && username[u_len - 1] == '\n') username[u_len - 1] = '\0';

        sys_write(1, "Password: ", 10);
        int p_len = 0;
        while (p_len < 31) {
            char c;
            int r = sys_read(0, &c, 1);
            if (r <= 0) break;
            if (c == '\n') break;
            password[p_len++] = c;
        }
        password[p_len] = '\0';

        uint32_t hash = 0;
        for (int i = 0; password[i]; i++) {
            hash = hash * 31 + (uint8_t)password[i];
        }

        if (strcmp(username, "root") == 0 && strcmp(password, "1234") == 0) {
            sys_write(1, "Login successful\n", 17);
            int pid = sys_fork();
            if (pid == 0) {
                char *argv[] = { "/bin/shell", 0 };
                sys_execve("/bin/shell", argv, 0);
                sys_exit(1);
            }
            int status;
            sys_waitpid(pid, &status);
        } else {
            sys_write(1, "Login incorrect\n", 16);
        }
    }

    return 0;
}
