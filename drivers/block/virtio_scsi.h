#ifndef VIRTIO_SCSI_H
#define VIRTIO_SCSI_H

#include "stdint.h"

/* VirtIO SCSI host driver.
 *
 * Implements virtio-scsi (virtio spec 5.9). Provides per-target LUN
 * discovery and SCSI command submission via a single command virtqueue.
 *
 * The driver handles the basic READ_10 / WRITE_10 / INQUIRY commands
 * needed by a generic block layer. Higher-level features like hotplug
 * events, multiple channels, and TCQ are out of scope.
 */

#define VIRTIO_SCSI_VENDOR_ID       0x1AF4
#define VIRTIO_SCSI_DEVICE_ID       0x1004

/* virtqueue indices */
#define VIRTIO_SCSI_VQ_CONTROL      0
#define VIRTIO_SCSI_VQ_EVENT        1
#define VIRTIO_SCSI_VQ_REQUEST      2

/* Maximum number of targets / LUNs per target */
#define VIRTIO_SCSI_MAX_TARGETS     256
#define VIRTIO_SCSI_MAX_LUNS        256
#define VIRTIO_SCSI_MAX_IN_FLIGHT   64

/* SCSI opcodes (subset) */
#define SCSI_OP_INQUIRY             0x12
#define SCSI_OP_TEST_UNIT_READY     0x00
#define SCSI_OP_READ_CAPACITY_10    0x25
#define SCSI_OP_READ_10             0x28
#define SCSI_OP_WRITE_10            0x2A
#define SCSI_OP_REQUEST_SENSE       0x03

/* SCSI status codes */
#define SCSI_STATUS_GOOD            0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02

/* virtio_scsi_req_cmd command types */
#define VIRTIO_SCSI_CMD_T_TMF       0x00
#define VIRTIO_SCSI_CMD_T_PROCESS   0x01

/* Response status codes (in virtio_scsi_resp_cmd) */
#define VIRTIO_SCSI_OK              0
#define VIRTIO_SCSI_OVERRUN         1
#define VIRTIO_SCSI_ABORTED         2
#define VIRTIO_SCSI_BAD_TARGET      3
#define VIRTIO_SCSI_RESET           4
#define VIRTIO_SCSI_TRANSPORT_FAIL  5
#define VIRTIO_SCSI_TARGET_FAILURE  6
#define VIRTIO_SCSI_NEXUS_FAILURE   7
#define VIRTIO_SCSI_SENSE           8

/* Per-LUN state */
typedef struct {
    uint32_t target;
    uint32_t lun;
    uint32_t block_count;
    uint32_t block_size;
    int      present;
    char     vendor[8];
    char     product[16];
    char     revision[4];
} virtio_scsi_lun_t;

int virtio_scsi_init(void);
int virtio_scsi_get_lun_count(void);
const virtio_scsi_lun_t *virtio_scsi_get_lun(int idx);
int virtio_scsi_read(int lun_idx, uint32_t lba, uint32_t blocks, void *buf);
int virtio_scsi_write(int lun_idx, uint32_t lba, uint32_t blocks, const void *buf);
int virtio_scsi_inquiry(int lun_idx, char *vendor, char *product, char *rev);

#endif