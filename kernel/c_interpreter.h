#ifndef C_INTERPRETER_H
#define C_INTERPRETER_H

void cinterp_init(void);
void cinterp_run(void);
int cinterp_exec(const char *code);
void cinterp_repl(void);

#endif
