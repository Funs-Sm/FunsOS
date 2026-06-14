#ifndef VSERIAL_H
#define VSERIAL_H

#include "stdint.h"
#include "kernel_types.h"

#define VSERIAL_BUF_SIZE 4096
#define VSERIAL_PORT     0x3E8  /* COM3 base I/O port */

void vserial_init(void);
void vserial_handler(regs_t *regs);
int  vserial_write(const char *buf, uint32_t len);
int  vserial_read(char *buf, uint32_t maxlen);
int  vserial_available(void);

#endif
