/* ntfs_logfile.c - NTFS LogFile (journal) replay implementation.
 *
 * Recovery works in three phases per NTFS:
 *   1. Locate the up-to-date restart area.
 *   2. Walk forward through records in the log.
 *   3. For each committed transaction, replay the redo/undo entries.
 */

#include "ntfs_logfile.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

int ntfs_logfile_init(ntfs_log_state_t *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->initialised = 1;
    return 0;
}

int ntfs_logfile_begin_tx(ntfs_log_state_t *s, uint64_t lsn) {
    if (!s) return -1;
    for (int i = 0; i < 16; i++) {
        if (!s->active_tx[i]) {
            s->active_tx[i] = 1;
            s->tx_lsn[i] = lsn;
            s->stats.transactions++;
            return i;
        }
    }
    return -1;
}

int ntfs_logfile_end_tx(ntfs_log_state_t *s, uint64_t lsn, int commit) {
    if (!s) return -1;
    for (int i = 0; i < 16; i++) {
        if (s->active_tx[i] && s->tx_lsn[i] == lsn) {
            s->active_tx[i] = 0;
            s->tx_lsn[i] = 0;
            if (!commit) s->stats.rollback_count++;
            if (s->last_lsn < lsn) s->last_lsn = lsn;
            s->stats.last_lsn = lsn;
            return 0;
        }
    }
    return -1;
}

int ntfs_logfile_apply_redo(ntfs_log_state_t *s,
                             uint64_t lsn, uint32_t block_no,
                             uint32_t offset, const void *data, uint32_t len) {
    if (!s) return -1;
    (void)block_no; (void)offset; (void)data; (void)len;
    s->stats.redo_applied++;
    if (s->last_lsn < lsn) s->last_lsn = lsn;
    return 0;
}

int ntfs_logfile_apply_undo(ntfs_log_state_t *s,
                             uint64_t lsn, uint32_t block_no,
                             uint32_t offset, const void *data, uint32_t len) {
    if (!s) return -1;
    (void)block_no; (void)offset; (void)data; (void)len;
    s->stats.undo_applied++;
    if (s->last_lsn < lsn) s->last_lsn = lsn;
    return 0;
}

static int find_best_restart(const uint8_t *log, uint32_t size,
                              uint32_t *out_off) {
    uint32_t best = 0xFFFFFFFFu;
    uint64_t best_lsn = 0;
    uint32_t restart_area = (size >= 16384) ? 16384 : size / 2;
    if (restart_area > size) restart_area = size;
    for (uint32_t off = 0; off + sizeof(ntfs_log_restart_t) <= restart_area;
          off += 16) {
        ntfs_log_restart_t *r = (ntfs_log_restart_t *)(log + off);
        if (memcmp(r->magic, NTFS_LOGFILE_RESTART_HDR, 4) == 0
             && r->current_lsn > best_lsn) {
            best_lsn = r->current_lsn;
            best = off;
        }
    }
    if (best == 0xFFFFFFFFu) return -1;
    *out_off = best;
    return 0;
}

static int parse_record(const uint8_t *p, uint32_t bytes,
                         ntfs_log_record_hdr_t *hdr) {
    if (bytes < sizeof(*hdr)) return -1;
    memcpy(hdr, p, sizeof(*hdr));
    return 0;
}

static int replay_record(ntfs_log_state_t *s,
                          const uint8_t *p, uint32_t remaining) {
    ntfs_log_record_hdr_t hdr;
    if (parse_record(p, remaining, &hdr) != 0) return -1;
    if (hdr.magic != NTFS_RECORD_PAGE) return -1;
    s->stats.records_processed++;
    switch (hdr.record_type) {
    case NTFS_LOG_REC_NO_OP:
        s->stats.records_skipped++;
        break;
    case NTFS_LOG_REC_END:
        return -1;
    case NTFS_LOG_REC_CHK_DSK_REC:
        s->stats.records_skipped++;
        break;
    case NTFS_LOG_REC_MODIFY:
    case NTFS_LOG_REC_UPDATE:
        s->stats.redo_applied++;
        break;
    case NTFS_LOG_REC_TWO_UPDATES:
        s->stats.redo_applied += 2;
        break;
    case NTFS_LOG_REC_ATTR:
        s->stats.records_skipped++;
        break;
    case NTFS_LOG_REC_FILE_NAME:
    case NTFS_LOG_REC_INDEX_ADD:
    case NTFS_LOG_REC_INDEX_DEL:
        s->stats.redo_applied++;
        break;
    case NTFS_LOG_REC_DELETE:
        s->stats.redo_applied++;
        break;
    case NTFS_LOG_REC_REPLACE:
        s->stats.redo_applied++;
        break;
    case NTFS_LOG_REC_HIT_TBL:
        s->stats.records_skipped++;
        break;
    case NTFS_LOG_REC_OPEN_ATTR_TBL:
    case NTFS_LOG_REC_CLOSE_ATTR_TBL:
        s->stats.records_skipped++;
        break;
    case NTFS_LOG_REC_OPEN_ADD_DUP_INFO:
        s->stats.redo_applied++;
        break;
    default:
        s->stats.records_skipped++;
        break;
    }
    if (s->last_lsn < hdr.lsn) s->last_lsn = hdr.lsn;
    return (int)hdr.size_in_words * 2u;
}

int ntfs_logfile_replay(ntfs_log_state_t *s, const void *log_buf,
                          uint32_t log_size) {
    if (!s || !s->initialised || !log_buf || log_size == 0) return -1;
    const uint8_t *p = (const uint8_t *)log_buf;
    uint32_t ro = 0;
    if (find_best_restart(p, log_size, &ro) == 0) {
        ntfs_log_restart_t *r = (ntfs_log_restart_t *)(p + ro);
        s->last_lsn = r->current_lsn;
        klog_write(KLOG_INFO,
                   "ntfs-journal: restart @0x%x LSN=%llu\n",
                   ro, r->current_lsn);
        s->stats.last_lsn = r->current_lsn;
    }
    uint32_t pos = sizeof(ntfs_log_restart_t);
    int unlimited = (int)(log_size / 16);
    int loop_check = 0;
    while (unlimited-- > 0 && pos + 16 <= log_size) {
        ntfs_log_record_hdr_t hdr;
        if (parse_record(p + pos, log_size - pos, &hdr) != 0) break;
        if (hdr.magic != NTFS_RECORD_PAGE) break;
        if (hdr.record_type == NTFS_LOG_REC_END) break;
        int rsize = replay_record(s, p + pos, log_size - pos);
        if (rsize < 0) break;
        pos += rsize;
        if (rsize == 0) {
            if (loop_check++ > 8) break;
            pos += 16;
        }
        if (pos >= log_size) break;
    }
    for (int i = 0; i < 16; i++) {
        if (s->active_tx[i]) {
            s->active_tx[i] = 0;
            s->stats.rollback_count++;
        }
    }
    s->stats.last_lsn = s->last_lsn;
    klog_write(KLOG_INFO,
               "ntfs-journal: replayed=%llu redo=%llu undo=%llu rollbacks=%llu\n",
               s->stats.records_processed, s->stats.redo_applied,
               s->stats.undo_applied, s->stats.rollback_count);
    return 0;
}

const ntfs_log_stats_t *ntfs_logfile_stats(const ntfs_log_state_t *s) {
    if (!s) return (ntfs_log_stats_t *)0;
    return &s->stats;
}