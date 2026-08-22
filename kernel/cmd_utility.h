/*
 * kernel/cmd_utility.h - User-facing utility commands (PR-5, v0.8.5)
 *
 *   which CMD          - locate a command in the builtin table / PATH dirs
 *   type CMD           - report builtin / alias / file
 *   tee [-a] FILE...   - copy stdin to FILEs (in our case, copy arg lines)
 *   xargs [-n N] CMD   - read lines from stdin and append as args
 *   test EXPR | [ EXPR ] - POSIX expression evaluator
 *   expr EXPR          - integer / string arithmetic evaluator
 *   install SRC DST    - copy + chmod (mode)
 */
#ifndef _KERNEL_CMD_UTILITY_H
#define _KERNEL_CMD_UTILITY_H

void cmd_which(const char *args);
void cmd_type(const char *args);
void cmd_tee(const char *args);
void cmd_xargs(const char *args);
void cmd_test(const char *args);
void cmd_expr(const char *args);
void cmd_install(const char *args);

#endif