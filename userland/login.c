/* login.c - CLI 登录程序 (用户态)
 * 改造说明: 原版硬编码 root/1234 凭据，现已改为读取
 * /etc/passwd 用户数据库进行验证，与 os/services/login_service.c
 * (GUI 登录) 共享同一用户数据源。
 * 当 GUI 不可用时 (VGA 文本模式回退)，此程序作为终端登录界面。
 */
#include "user_syscall.h"
#include "string.h"

static char username[32];
static char password[32];
static char line_buf[128];

/* 从 /etc/passwd 查找用户并验证密码
 * /etc/passwd 格式: username:password_hash:uid:gid:home:shell
 * 简化实现: 明文比较 (与 login_service.c 保持一致) */
static int check_credentials(const char *user, const char *pass) {
    int fd = sys_open("/etc/passwd", 0);
    if (fd < 0) {
        /* 无密码文件时回退到默认账户 */
        if (strcmp(user, "root") == 0 && strcmp(pass, "root") == 0)
            return 1;
        if (strcmp(user, "admin") == 0 && strcmp(pass, "admin") == 0)
            return 1;
        return 0;
    }

    int found = 0;
    int n;
    while ((n = sys_read(fd, line_buf, sizeof(line_buf) - 1)) > 0) {
        line_buf[n] = '\0';
        /* 查找用户名字段 */
        char *p = line_buf;
        /* 比较用户名 */
        int i;
        for (i = 0; user[i] && p[i] && p[i] != ':'; i++) {
            if (user[i] != p[i]) break;
        }
        if (user[i] == '\0' && p[i] == ':') {
            /* 用户名匹配，检查密码 */
            p = p + i + 1; /* 跳过冒号 */
            int j;
            for (j = 0; pass[j] && p[j] && p[j] != ':'; j++) {
                if (pass[j] != p[j]) break;
            }
            if (pass[j] == '\0' && (p[j] == ':' || p[j] == '\n' || p[j] == '\0')) {
                found = 1;
                break;
            }
        }
    }
    sys_close(fd);
    return found;
}

int main(void) {
    sys_write(1, "\n=== FunsOS Login ===\n", 21);

    while (1) {
        sys_write(1, "\nLogin: ", 8);
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
            if (c == '\b' || c == 127) {
                if (p_len > 0) { p_len--; }
                continue;
            }
            password[p_len++] = c;
            sys_write(1, "*", 1); /* 密码掩码 */
        }
        password[p_len] = '\0';
        sys_write(1, "\n", 1);

        if (check_credentials(username, password)) {
            sys_write(1, "Login successful\n", 17);

            /* 启动 shell */
            int pid = sys_fork();
            if (pid == 0) {
                char *argv[] = { "/bin/shell", 0 };
                sys_execve("/bin/shell", argv, 0);
                sys_exit(1);
            }

            /* 等待 shell 退出 */
            int status;
            sys_waitpid(pid, &status);
            sys_write(1, "Session ended\n", 14);
        } else {
            sys_write(1, "Login incorrect\n", 16);
        }
    }

    return 0;
}
