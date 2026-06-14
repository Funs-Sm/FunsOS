#ifndef LOOPBACK_H
#define LOOPBACK_H

#include "net.h"

/* Software loopback interface.  Initialised automatically by
 * net_init() and named "lo".  Carries 127.0.0.1/8 with no gateway.
 *
 * The driver is fully self-contained: outgoing frames are simply
 * enqueued for local consumption (and delivered to ethernet_receive()
 * on the same interface), allowing applications to talk to themselves
 * without any hardware.  No stats counters are needed because the
 * normal net/iface stats suffice.  In addition, the driver also
 * advertises the IFF_LOOPBACK and IFF_UP flags automatically. */
void loopback_install(void);

/* Return a pointer to the loopback interface once installed.  May
 * return NULL if the device table is full. */
net_interface_t *loopback_get(void);

#endif
