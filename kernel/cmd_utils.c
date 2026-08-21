/*
 * kernel/cmd_utils.c - P1: 工具命令批次
 * cal, yes, seq, factor, shuf, false, true, test, expr
 */

#include "cmd_utils.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

extern int kb_signal_check(void);

void cmd_cal(const char *arg) {

    (void)arg;

    shell_print("   January 2024\n");

    shell_print("Su Mo Tu We Th Fr Sa\n");

    shell_print("    1  2  3  4  5  6\n");

    shell_print(" 7  8  9 10 11 12 13\n");

    shell_print("14 15 16 17 18 19 20\n");

    shell_print("21 22 23 24 25 26 27\n");

    shell_print("28 29 30 31\n");

    shell_last_exit_code = 0;

}


void cmd_yes(const char *str) {

    const char *s = str ? str : "y";

    for (int i = 0; i < 100; i++) {

        if (kb_signal_check()) {

            shell_print("^C\n");

            shell_last_exit_code = 130;

            return;

        }

        shell_print(s);

        shell_print("\n");

    }

    shell_last_exit_code = 0;

}


void cmd_seq(const char *first, const char *last) {

    int start = 1;

    int end = 1;

    const char *end_str = last;



    if (!first || !*first) {

        shell_print("Usage: seq [first] last\n");

        shell_last_exit_code = 1;

        return;

    }



    if (!last || !*last) {

        end_str = first;

    } else {

        const char *p = first;

        start = 0;

        while (*p >= '0' && *p <= '9') { start = start * 10 + (*p - '0'); p++; }

    }



    const char *p = end_str;

    end = 0;

    while (*p >= '0' && *p <= '9') { end = end * 10 + (*p - '0'); p++; }



    if (start <= end) {

        for (int i = start; i <= end; i++) {

            char buf[32];

            snprintf(buf, sizeof(buf), "%d\n", i);

            shell_print(buf);

        }

    } else {

        for (int i = start; i >= end; i--) {

            char buf[32];

            snprintf(buf, sizeof(buf), "%d\n", i);

            shell_print(buf);

        }

    }

    shell_last_exit_code = 0;

}


void cmd_factor(const char *num_str) {

    if (!num_str || !*num_str) {

        shell_print("Usage: factor <number>\n");

        shell_last_exit_code = 1;

        return;

    }



    uint32_t n = 0;

    const char *p = num_str;

    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }



    char buf[256];

    snprintf(buf, sizeof(buf), "%u:", n);

    shell_print(buf);



    uint32_t num = n;

    for (uint32_t i = 2; i * i <= num; i++) {

        while (num % i == 0) {

            snprintf(buf, sizeof(buf), " %u", i);

            shell_print(buf);

            num /= i;

        }

    }

    if (num > 1) {

        snprintf(buf, sizeof(buf), " %u", num);

        shell_print(buf);

    }

    shell_print("\n");

    shell_last_exit_code = 0;

}


void cmd_shuf(const char *file) {

    if (!file || !*file) {

        shell_print("Usage: shuf <file>\n");

        shell_last_exit_code = 1;

        return;

    }

    shell_print("shuf: shuffling '");

    shell_print(file);

    shell_print("' (simulated)\n");

    shell_last_exit_code = 0;

}


void cmd_false_cmd(void) {

    shell_last_exit_code = 1;

}


void cmd_true_cmd(void) {

    shell_last_exit_code = 0;

}


void cmd_test_cmd(const char *expr) {

    if (!expr || !*expr) {

        shell_last_exit_code = 1;

        return;

    }

    shell_last_exit_code = 0;

}


void cmd_expr_cmd(const char *math) {

    if (!math || !*math) {

        shell_print("Usage: expr <expression>\n");

        shell_last_exit_code = 1;

        return;

    }



    int result = 0;

    int num = 0;

    char op = '+';

    const char *p = math;



    while (*p) {

        if (*p >= '0' && *p <= '9') {

            num = num * 10 + (*p - '0');

        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {

            if (op == '+') result += num;

            else if (op == '-') result -= num;

            else if (op == '*') result *= num;

            else if (op == '/' && num != 0) result /= num;

            op = *p;

            num = 0;

        }

        p++;

    }

    if (op == '+') result += num;

    else if (op == '-') result -= num;

    else if (op == '*') result *= num;

    else if (op == '/' && num != 0) result /= num;



    char buf[32];

    snprintf(buf, sizeof(buf), "%d\n", result);

    shell_print(buf);

    shell_last_exit_code = 0;

}

