#include "user_syscall.h"
#include "string.h"

static char cwd[256] = "/";
static char input[256];

static void print_prompt(void)
{
    sys_write(1, cwd, strlen(cwd));
    sys_write(1, "$ ", 2);
}

static void builtin_cd(char *args[], int argc)
{
    if (argc < 2) {
        sys_write(2, "cd: missing argument\n", 21);
        return;
    }
    if (sys_chdir(args[1]) < 0) {
        sys_write(2, "cd: failed\n", 11);
        return;
    }
    sys_getcwd(cwd, 256);
}

static void builtin_pwd(void)
{
    char buf[256];
    sys_getcwd(buf, 256);
    sys_write(1, buf, strlen(buf));
    sys_write(1, "\n", 1);
}

static void builtin_exit(void)
{
    sys_exit(0);
}

static void builtin_help(void)
{
    sys_write(1, "Available commands:\n", 20);
    sys_write(1, "  cd <path>  - Change directory\n", 32);
    sys_write(1, "  pwd        - Print working directory\n", 38);
    sys_write(1, "  exit       - Exit shell\n", 26);
    sys_write(1, "  help       - Show this help\n", 30);
    sys_write(1, "  <command>  - Execute external command\n", 40);
}

static int parse_command(char *input, char *args[], int max_args)
{
    int argc = 0;
    while (*input && argc < max_args) {
        while (*input == ' ' || *input == '\t') {
            input++;
        }
        if (!*input) break;
        args[argc++] = input;
        while (*input && *input != ' ' && *input != '\t') {
            input++;
        }
        if (*input) {
            *input++ = '\0';
        }
    }
    return argc;
}

static void execute_command(char *args[], int argc)
{
    if (argc == 0) return;

    if (strcmp(args[0], "cd") == 0) {
        builtin_cd(args, argc);
        return;
    }
    if (strcmp(args[0], "pwd") == 0) {
        builtin_pwd();
        return;
    }
    if (strcmp(args[0], "exit") == 0) {
        builtin_exit();
        return;
    }
    if (strcmp(args[0], "help") == 0) {
        builtin_help();
        return;
    }

    int pid = sys_fork();
    if (pid == 0) {
        sys_exec(args[0], args);
        sys_write(2, args[0], strlen(args[0]));
        sys_write(2, ": command not found\n", 20);
        sys_exit(1);
    } else if (pid > 0) {
        sys_waitpid(-1, 0);
    } else {
        sys_write(2, "fork failed\n", 12);
    }
}

int main(int argc, char *argv[])
{
    sys_write(1, "Shell v1.0\n", 11);

    while (1) {
        print_prompt();
        int n = sys_read(0, input, 255);
        if (n <= 0) {
            sys_yield();
            continue;
        }
        input[n] = '\0';
        char *nl = strchr(input, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(input, '\r');
        if (cr) *cr = '\0';

        if (strlen(input) == 0) continue;

        char *args[32];
        int arg_count = parse_command(input, args, 32);
        execute_command(args, arg_count);
    }

    return 0;
}
