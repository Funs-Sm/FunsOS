#include "user_syscall.h"
#include "string.h"

static char input[256];
static double num_stack[64];
static int num_top = 0;
static char op_stack[64];
static int op_top = 0;

static void num_push(double val)
{
    num_stack[num_top++] = val;
}

static double num_pop(void)
{
    return num_stack[--num_top];
}

static double num_peek(void)
{
    return num_stack[num_top - 1];
}

static void op_push(char op)
{
    op_stack[op_top++] = op;
}

static char op_pop(void)
{
    return op_stack[--op_top];
}

static char op_peek(void)
{
    if (op_top == 0) return '\0';
    return op_stack[op_top - 1];
}

static int precedence(char op)
{
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    if (op == '^') return 3;
    return 0;
}

static void apply_op(void)
{
    char op = op_pop();
    double b = num_pop();
    double a = num_pop();
    double result = 0;
    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': result = b != 0 ? a / b : 0; break;
        case '^': {
            result = 1;
            int exp = (int)b;
            double base = a;
            if (exp >= 0) {
                for (int i = 0; i < exp; i++) result *= base;
            } else {
                for (int i = 0; i < -exp; i++) result *= base;
                if (result != 0) result = 1.0 / result;
            }
            break;
        }
    }
    num_push(result);
}

static double parse_number(const char *str, int *i)
{
    double result = 0;
    double fraction = 0.1;
    int has_dot = 0;

    while (str[*i] >= '0' && str[*i] <= '9' || (str[*i] == '.' && !has_dot)) {
        if (str[*i] == '.') {
            has_dot = 1;
            (*i)++;
            continue;
        }
        if (has_dot) {
            result += (str[*i] - '0') * fraction;
            fraction *= 0.1;
        } else {
            result = result * 10 + (str[*i] - '0');
        }
        (*i)++;
    }
    (*i)--;
    return result;
}

static double my_sqrt(double x)
{
    if (x < 0) return 0;
    if (x == 0) return 0;
    double guess = x / 2.0;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

static double parse_expression(const char *expr)
{
    num_top = 0;
    op_top = 0;
    int len = strlen(expr);
    int expect_operand = 1;

    for (int i = 0; i < len; i++) {
        if (expr[i] == ' ' || expr[i] == '\t') continue;

        if ((expr[i] >= '0' && expr[i] <= '9') || expr[i] == '.') {
            num_push(parse_number(expr, &i));
            expect_operand = 0;
            continue;
        }

        if (expr[i] == '(') {
            op_push('(');
            expect_operand = 1;
            continue;
        }

        if (expr[i] == ')') {
            while (op_top > 0 && op_peek() != '(') {
                apply_op();
            }
            if (op_top > 0) op_pop();
            expect_operand = 0;
            continue;
        }

        if (expr[i] == '-' && expect_operand) {
            i++;
            double val = 0;
            if (expr[i] >= '0' && expr[i] <= '9' || expr[i] == '.') {
                val = parse_number(expr, &i);
            }
            num_push(-val);
            expect_operand = 0;
            continue;
        }

        if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/' || expr[i] == '^') {
            while (op_top > 0 && op_peek() != '(' && precedence(op_peek()) >= precedence(expr[i])) {
                apply_op();
            }
            op_push(expr[i]);
            expect_operand = 1;
            continue;
        }
    }

    while (op_top > 0) {
        apply_op();
    }

    if (num_top > 0) return num_stack[0];
    return 0;
}

static void double_to_str(double val, char *buf)
{
    if (val < 0) {
        *buf++ = '-';
        val = -val;
    }

    long integer = (long)val;
    double frac = val - (double)integer;

    char tmp[32];
    int idx = 0;
    if (integer == 0) {
        tmp[idx++] = '0';
    } else {
        while (integer > 0) {
            tmp[idx++] = '0' + (integer % 10);
            integer /= 10;
        }
    }
    for (int i = idx - 1; i >= 0; i--) {
        *buf++ = tmp[i];
    }

    double check_frac = frac;
    int has_frac = 0;
    for (int i = 0; i < 6; i++) {
        check_frac *= 10;
        int d = (int)check_frac;
        if (d != 0) has_frac = 1;
        check_frac -= d;
    }

    if (has_frac) {
        *buf++ = '.';
        for (int i = 0; i < 6; i++) {
            frac *= 10;
            int d = (int)frac;
            *buf++ = '0' + d;
            frac -= d;
        }
        buf--;
        while (*buf == '0') buf--;
        if (*buf == '.') buf--;
        buf++;
    }

    *buf = '\0';
}

int main(int argc, char *argv[])
{
    sys_write(1, "Calculator v1.0 (type 'quit' to exit)\n", 38);

    while (1) {
        sys_write(1, "> ", 2);
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
        if (strcmp(input, "quit") == 0) break;

        double result = parse_expression(input);
        char buf[64];
        double_to_str(result, buf);
        sys_write(1, buf, strlen(buf));
        sys_write(1, "\n", 1);
    }

    sys_exit(0);
    return 0;
}
