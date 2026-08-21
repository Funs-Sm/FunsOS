#ifndef NTFS_LOGFILE_H
#define NTFS_LOGFILE_H

/* NTFS LogFile (journal) replay implementation.
 *
 * NTFS uses a simple redo logfile located at MFT index 2.
 * Three different restart pages may exist within a transactional
 * record; recovery re-applies the operations described by a
 * sequence of LFS (log file service) restart, attribute, and
 * modification records.
 *
 * This implementation tracks the logfile usage and rewrites
 * metadata blocks according to entries recorded in the journal.
 * It does NOT try to be a complete Windows NTFS implementation -
 * it provides a useful subset:
 *
 *   - RESTART_PAGE: reads the up-to-date restart page
 *   - RECORD_HEADER: walks records, verifies signature
 *   - MODIFICATION entries: re-applies "compensated" writes
 *   - STREAM attribute update records
 *
 * Sufficient for post-crash recovery validation in our environment.
 */

#include "stdint.h"

#define NTFS_LOGFILE_MAGIC       0x52545253
#define NTFS_LOGFILE_RESTART_HDR "RSTR"
#define NTFS_RECORD_PAGE         0x53555246

/* LogFile record types (NTFS LFS / NTFS_LOG). */
#define NTFS_LOG_REC_NO_OP       0
#define NTFS_LOG_REC_HIT_TBL     1
#define NTFS_LOG_REC_ATTR        4
#define NTFS_LOG_REC_FILE_NAME   5
#define NTFS_LOG_REC_INDEX_ADD   6
#define NTFS_LOG_REC_INDEX_DEL   7
#define NTFS_LOG_REC_MODIFY      8
#define NTFS_LOG_REC_UPDATE      9
#define NTFS_LOG_REC_TWO_UPDATES 10
#define NTFS_LOG_REC_DELETE      11
#define NTFS_LOG_REC_REPLACE     12
#define NTFS_LOG_REC_OPEN_ATTR_TBL 13
#define NTFS_LOG_REC_CLOSE_ATTR_TBL 14
#define NTFS_LOG_REC_OPEN_ADD_DUP_INFO 15
#define NTFS_LOG_REC_END          0xFFFE
#define NTFS_LOG_REC_CHK_DSK_REC  0xFFFF

/* Transaction state types for replay context. */
typedef enum {
    NTFS_LOG_TX_IDLE = 0,
    NTFS_LOG_TX_VIEW,
    NTFS_LOG_TX_REDO,
    NTFS_LOG_TX_UNDO,
} ntfs_log_tx_state_t;

typedef struct __attribute__((packed)) {
    char    magic[4];
    uint32_t data_length;
    int8_t  system_page_size;
    uint8_t  log_page_size;
    uint16_t restart_spacing;
    uint16_t num_restart_log;
    uint16_t first_offset;
    uint64_t chkdsk_lsn;
    uint32_t restart_log_offset;
    uint64_t current_lsn;
    uint64_t restart_open_log_id;
    uint8_t  flags;
    uint8_t  reserved[35];
    uint32_t signature;
} ntfs_log_restart_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t offset_to_usn;
    uint16_t size_in_words;
    uint64_t lsn;
    uint16_t client_idx;
    uint16_t record_type;
} ntfs_log_record_hdr_t;

typedef struct {
    uint64_t records_processed;
    uint64_t records_skipped;
    uint64_t redo_applied;
    uint64_t undo_applied;
    uint64_t pages_examined;
    uint64_t transactions;
    uint64_t last_lsn;
    uint64_t rollback_count;
} ntfs_log_stats_t;

typedef struct {
    int initialised;
    uint64_t last_lsn;
    int active_tx[16];
    uint64_t tx_lsn[16];
    ntfs_log_stats_t stats;
} ntfs_log_state_t;

int ntfs_logfile_init(ntfs_log_state_t *s);
int ntfs_logfile_replay(ntfs_log_state_t *s, const void *log_buf,
                        uint32_t log_size);

int ntfs_logfile_begin_tx(ntfs_log_state_t *s, uint64_t lsn);
int ntfs_logfile_end_tx(ntfs_log_state_t *s, uint64_t lsn, int commit);
int ntfs_logfile_apply_redo(ntfs_log_state_t *s,
                             uint64_t lsn, uint32_t block_no,
                             uint32_t offset, const void *data, uint32_t len);
int ntfs_logfile_apply_undo(ntfs_log_state_t *s,
                             uint64_t lsn, uint32_t block_no,
                             uint32_t offset, const void *data, uint32_t len);

const ntfs_log_stats_t *ntfs_logfile_stats(const ntfs_log_state_t *s);

#endif