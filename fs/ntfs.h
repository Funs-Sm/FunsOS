#ifndef NTFS_H
#define NTFS_H

#include "stdint.h"
#include "vfs.h"

/* NTFS filesystem driver.
 *
 * Implements a read-only NTFS driver capable of:
 *   - Reading the boot sector and verifying OEM signature
 *   - Walking the MFT (Master File Table) to enumerate files
 *   - Reading $STANDARD_INFORMATION, $FILE_NAME, $DATA attributes
 *   - Resolving attribute runs and reading file data
 *   - Following file-name index entries
 *
 * The driver does NOT implement:
 *   - Journaling / journal replay
 *   - File-system modifications (create, write, delete)
 *   - Compression / encryption / sparse file handling
 *
 * The implementation is suitable for read-only mounts and forensic /
 * recovery use cases where the goal is data extraction.
 */

#define NTFS_OEM_SIGNATURE       "NTFS    "
#define NTFS_OEM_SIGNATURE_LEN   8

/* MFT record signatures */
#define NTFS_MFT_MAGIC           0x454C4946   /* "FILE" */
#define NTFS_END_MARKER          0xFFFFFFFF

/* Attribute types */
#define NTFS_ATTR_STD_INFO       0x10
#define NTFS_ATTR_FILE_NAME      0x30
#define NTFS_ATTR_DATA           0x80
#define NTFS_ATTR_INDEX_ROOT     0x90
#define NTFS_ATTR_INDEX_ALLOC    0xA0
#define NTFS_ATTR_BITMAP         0xB0

/* Index entry flags */
#define NTFS_INDEX_ENTRY_END     0x02
#define NTFS_INDEX_ENTRY_CHILD  0x01

/* Boot sector layout (NTFS only uses 512 byte sector). */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint8_t  _reserved[7];
    uint8_t  media_descriptor;
    uint8_t  _reserved2[2];
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t _reserved3;
    uint32_t _reserved4;
    uint64_t total_sectors;
    uint64_t mft_lcn;          /* LCN of MFT (clusters) */
    uint64_t mftmirr_lcn;
    int8_t   clusters_per_mft_record;   /* negative value: shift */
    uint8_t  _pad1[3];
    int8_t   clusters_per_index_record;
    uint8_t  _pad2[3];
    uint64_t serial_number;
} ntfs_boot_sector_t;

/* MFT file record header */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];
    uint16_t update_seq_offset;
    uint16_t update_seq_size;
    uint64_t logfile_seq;
    uint16_t sequence_number;
    uint16_t hard_link_count;
    uint16_t first_attr_offset;
    uint16_t flags;
    uint32_t used_size;
    uint32_t allocated_size;
    uint64_t base_record;
    uint16_t next_attr_id;
    uint16_t pad;
} ntfs_mft_record_t;

/* Attribute header (non-resident / resident) */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t length;
    uint8_t  non_resident;
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attribute_id;
} ntfs_attr_header_t;

/* Resident attribute body */
typedef struct __attribute__((packed)) {
    uint32_t content_size;
    uint16_t content_offset;
    uint8_t  indexed;
    uint8_t  _pad;
} ntfs_attr_resident_t;

/* Non-resident attribute body */
typedef struct __attribute__((packed)) {
    uint64_t start_vcn;
    uint64_t end_vcn;
    uint16_t run_list_offset;
    uint16_t compression_unit;
    uint32_t _pad;
    uint64_t alloc_size;
    uint64_t real_size;
    uint64_t init_size;
} ntfs_attr_nonresident_t;

/* Run list entry: variable-length byte encoding */
typedef struct {
    uint64_t start_lcn;
    int64_t  length;
} ntfs_run_t;

/* Public driver state. */
typedef struct {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint64_t total_clusters;
    uint64_t mft_lcn;
    uint32_t mft_record_size;
    uint8_t  drive;
    uint32_t partition_start;     /* LBA offset of partition */
    int      inited;
    /* Cache of MFT inode -> first record. */
    uint64_t mft_inode_lcn;       /* LCN where MFT data begins */
    /* Stats. */
    uint64_t reads;
    uint64_t bytes_read;
} ntfs_state_t;

/* Public API */
int ntfs_init(uint8_t drive, uint32_t partition_start);
int ntfs_read_boot_sector(uint8_t drive, uint32_t partition_start,
                          ntfs_boot_sector_t *bs);
int ntfs_read_mft_record(uint64_t mft_index,
                         ntfs_mft_record_t *rec, void *buf);
int ntfs_resolve_data_run(const uint8_t *run_list, uint64_t *offset_lcn,
                           uint64_t *length);
int ntfs_read_file_data(uint64_t mft_index, uint64_t offset,
                        uint32_t length, void *buf);

const ntfs_state_t *ntfs_get_state(void);

#endif