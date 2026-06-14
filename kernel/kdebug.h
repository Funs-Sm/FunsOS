#ifndef KDEBUG_H
#define KDEBUG_H

#include "kernel_types.h"
#include "stdint.h"

/*
 * Kernel Debugger - Public API
 *
 * Provides:
 *  - kdebug_init()   : one-time initialisation at boot
 *  - kdebug_enter()  : enter debugger from exception/interrupt handler
 *  - kdebug_poll()   : poll serial port for debugger activation
 *  - kdebug_check_key() : check keyboard event for Ctrl+Shift+D
 *  - kdebug_is_active() : query if debugger is currently running
 */

void kdebug_init(void);
void kdebug_enter(regs_t *regs);
void kdebug_poll(void);
int  kdebug_check_key(uint8_t scancode, uint8_t flags);
int  kdebug_is_active(void);

#endif /* KDEBUG_H */