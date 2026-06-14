/* slip.h - SLIP (Serial Line IP) protocol driver */

#ifndef SLIP_H
#define SLIP_H

#include "stdint.h"

void slip_init(void);
int  slip_send(const uint8_t *buf, uint32_t len);
int  slip_recv(uint8_t *buf, uint32_t maxlen);

#endif /* SLIP_H */
