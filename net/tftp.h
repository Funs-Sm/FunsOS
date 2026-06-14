#ifndef TFTP_H
#define TFTP_H

#include "stdint.h"
#include "net.h"

/* TFTP opcodes (RFC 1350) */
#define TFTP_OPCODE_RRQ   1
#define TFTP_OPCODE_WRQ   2
#define TFTP_OPCODE_DATA  3
#define TFTP_OPCODE_ACK   4
#define TFTP_OPCODE_ERROR 5

/* TFTP error codes */
#define TFTP_ERR_NOT_DEFINED    0
#define TFTP_ERR_FILE_NOT_FOUND 1
#define TFTP_ERR_ACCESS         2
#define TFTP_ERR_DISK_FULL      3
#define TFTP_ERR_ILLEGAL_OP     4
#define TFTP_ERR_UNKNOWN_ID     5
#define TFTP_ERR_FILE_EXISTS    6
#define TFTP_ERR_NO_USER        7

#define TFTP_BLOCK_SIZE     512
#define TFTP_SERVER_PORT    69
#define TFTP_TIMEOUT_TICKS  500  /* ~5 seconds (assuming 10 ms/tick) */
#define TFTP_MAX_RETRIES    5
#define TFTP_MAX_FILE_SIZE  (4 * 1024 * 1024)

typedef struct __attribute__((packed)) {
    uint16_t opcode;
    union {
        struct {
            char filename_mode[1]; /* filename + '\0' + mode + '\0' */
        } rrq;
        struct {
            char filename_mode[1];
        } wrq;
        struct {
            uint16_t block_num;
            uint8_t  data[TFTP_BLOCK_SIZE];
        } data_pkt;
        struct {
            uint16_t block_num;
        } ack;
        struct {
            uint16_t error_code;
            char     error_msg[1];
        } error;
    } u;
} tftp_packet_t;

/**
 * Download a file from a TFTP server.
 *
 * @param server_ip  IP address string of the TFTP server (e.g. "192.168.1.1")
 * @param filename   Name of the file to retrieve
 * @param out_data   Pointer that will be set to the allocated buffer containing file data
 * @param out_size   Pointer that will receive the file size in bytes
 * @return 0 on success, negative on error
 *   -1  invalid argument
 *   -2  DNS resolution failed
 *   -3  no network interface
 *   -4  socket creation failed
 *   -5  bind failed
 *   -6  send failed
 *   -7  timeout
 *   -8  error packet from server
 *   -9  out of memory
 *  -10  file too large
 */
int tftp_get(const char *server_ip, const char *filename,
             uint8_t **out_data, uint32_t *out_size);

#endif
