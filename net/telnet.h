#ifndef TELNET_H
#define TELNET_H

#include "stdint.h"

/* Telnet commands (RFC 854) */
#define TELNET_IAC   255
#define TELNET_DONT  254
#define TELNET_DO    253
#define TELNET_WONT  252
#define TELNET_WILL  251
#define TELNET_SB    250
#define TELNET_SE    240

/* Default telnet port */
#define TELNET_DEFAULT_PORT 23

/**
 * Connect to a remote telnet server.
 *
 * @param host  Hostname or IP address string of the server
 * @param port  TCP port to connect to (use TELNET_DEFAULT_PORT for standard)
 * @return 0 on success, negative on error
 *   -1  invalid argument
 *   -2  DNS resolution failed
 *   -3  no network interface
 *   -4  connection failed
 */
int telnet_connect(const char *host, uint16_t port);

/**
 * Send data over the telnet connection.
 *
 * @param data  Data to send
 * @param len   Number of bytes to send
 * @return number of bytes sent on success, negative on error
 */
int telnet_send(const char *data, uint32_t len);

/**
 * Receive data from the telnet connection.
 * IAC sequences are handled internally (responding WONT/DONT to WILL/DO).
 * Only regular data bytes are returned to the caller.
 *
 * @param buf   Buffer to receive data
 * @param len   Buffer size
 * @return number of bytes received on success, 0 on disconnect, negative on error
 */
int32_t telnet_recv(char *buf, uint32_t len);

/**
 * Close the telnet connection and release resources.
 */
void telnet_close(void);

#endif
