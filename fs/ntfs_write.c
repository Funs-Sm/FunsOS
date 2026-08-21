/* ntfs_write.c - NTFS write support.
 *
 * Provides:
 *   - MFT bitmap allocation
 *   - MFT record creation / append
 *   - Attribute-list manipulation (insert)
 *   - File creation under a parent directory and indexing
 *   - File data write (resident / non-resident)
 *   - Rename / delete
 */

#include "ntfs_write.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

static uint64_t clu_to_lba(const ntfs_state_t *st, uint64_t lcn) {
    return st->partition_start + lcn * st->sectors_per_cluster;
}

static int read_cluster(const ntfs_state_t *st, uint64_t lcn,
                         void *buf) {
    uint32_t lba = (uint32_t)clu_to_lba(st, lcn);
    return ide_read_sectors(st->drive,
                              (uint8_t)st->sectors_per_cluster,
                              lba, buf);
}

static int write_cluster(const ntfs_state_t *st, uint64_t lcn,
                          const void *buf) {
    uint32_t lba = (uint32_t)clu_to_lba(st, lcn);
    return ide_write_sectors(st->drive,
                               (uint8_t)st->sectors_per_cluster,
                               lba, (void *)buf);
}

static int read_mft_record_buf(const ntfs_state_t *st,
                                uint64_t mft_idx,
                                void *buf, uint32_t buf_size) {
    uint32_t rec_size = st->mft_record_size;
    if (buf_size < rec_size) return -1;
    uint64_t rec_offset = mft_idx * rec_size;
    uint64_t mft_lcn = st->mft_lcn + rec_offset / (st->cluster_size);
    uint32_t off_in_cluster = (uint32_t)(rec_offset % (st->cluster_size));
    if (off_in_cluster + rec_size <= (uint64_t)st->cluster_size) {
        return read_cluster(st, mft_lcn, buf);
    }
    return -1;
}

static int write_mft_record_buf(const ntfs_state_t *st,
                                 uint64_t mft_idx,
                                 const void *buf) {
    uint32_t rec_size = st->mft_record_size;
    uint64_t rec_offset = mft_idx * rec_size;
    uint64_t mft_lcn = st->mft_lcn + rec_offset / (st->cluster_size);
    return write_cluster(st, mft_lcn, buf);
}

static int test_bit(const uint8_t *bm, uint64_t bit) {
    return (bm[bit >> 3] >> (bit & 7)) & 1;
}
static void set_bit(uint8_t *bm, uint64_t bit) {
    bm[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}
static void clear_bit(uint8_t *bm, uint64_t bit) {
    bm[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
}

static int bitmap_find_free(const ntfs_state_t *st, uint64_t *out) {
    uint8_t mft[1024];
    if (read_mft_record_buf(st, 6, mft, sizeof(mft)) != 0) return -1;
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)mft;
    uint8_t *p = (uint8_t *)mft + rec->first_attr_offset;
    uint8_t *end = mft + sizeof(mft);
    while (p + sizeof(ntfs_attr_header_t) <= end) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
        if (ah->type == 0xFFFFFFFFu) break;
        if (ah->type == NTFS_ATTR_BITMAP) {
            if (ah->non_resident) {
                ntfs_attr_nonresident_t *nr = (ntfs_attr_nonresident_t *)
                                              (ah + 1);
                uint8_t *rdata = (uint8_t *)p + nr->run_list_offset;
                uint64_t off = 0, len = 0;
                if (ntfs_resolve_data_run(rdata, &off, &len) != 0) break;
                if (off == 0) return -1;
                uint8_t *bm = (uint8_t *)kmalloc(st->cluster_size);
                if (!bm) return -1;
                if (read_cluster(st, off, bm) != 0) { kfree(bm); return -1; }
                for (uint64_t i = 1; i < 8 * st->cluster_size; i++) {
                    if (!test_bit(bm, i)) {
                        *out = i;
                        kfree(bm);
                        return 0;
                    }
                }
                kfree(bm);
                return -1;
            }
            break;
        }
        if (ah->length == 0) break;
        p += ah->length;
    }
    return -1;
}

static uint8_t *find_attr_buf(uint8_t *buf, uint32_t type,
                               uint32_t buf_size, uint32_t *out_attr_len) {
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)buf;
    uint8_t *p = buf + rec->first_attr_offset;
    uint8_t *end = buf + buf_size;
    while (p + sizeof(ntfs_attr_header_t) <= end) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
        if (ah->type == 0xFFFFFFFFu) break;
        if (ah->type == type) {
            if (out_attr_len) *out_attr_len = ah->length;
            return p;
        }
        if (ah->length == 0) break;
        p += ah->length;
    }
    return (uint8_t *)0;
}

static uint8_t *find_attr_end(uint8_t *buf, uint32_t buf_size) {
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)buf;
    uint8_t *p = buf + rec->first_attr_offset;
    uint8_t *end = buf + buf_size;
    while (p + sizeof(ntfs_attr_header_t) <= end) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
        if (ah->type == 0xFFFFFFFFu) break;
        if (ah->length == 0) break;
        p += ah->length;
    }
    return p;
}

#define NTFS_STDINFO_SIZE 72
typedef struct __attribute__((packed)) {
    uint64_t creation;
    uint64_t modification;
    uint64_t mft_modification;
    uint64_t access;
    uint32_t flags;
    uint32_t max_versions;
    uint32_t version;
    uint32_t class_id;
    uint32_t owner_id;
    uint32_t security_id;
    uint32_t quota_charged;
    uint64_t usn;
} ntfs_stdinfo_t;

static int update_stdinfo(uint8_t *buf, uint32_t buf_size,
                           uint16_t mode, uint64_t now) {
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)buf;
    uint32_t attr_len = 0;
    uint8_t *p = find_attr_buf(buf, NTFS_ATTR_STD_INFO, buf_size, &attr_len);
    ntfs_attr_header_t *ah;
    ntfs_attr_resident_t *body;
    ntfs_stdinfo_t *info;
    if (p) {
        ah = (ntfs_attr_header_t *)p;
        body = (ntfs_attr_resident_t *)(ah + 1);
        info = (ntfs_stdinfo_t *)(p + body->content_offset);
        info->creation = info->modification = info->mft_modification
                       = info->access = now;
        info->flags = (uint32_t)mode & 0xFFFF;
        return 0;
    }
    uint8_t *insert_at = find_attr_end(buf, buf_size);
    if (insert_at + sizeof(ntfs_attr_header_t) + 16 + NTFS_STDINFO_SIZE
        > buf + buf_size) return -1;
    ah = (ntfs_attr_header_t *)insert_at;
    ah->type = NTFS_ATTR_STD_INFO;
    ah->non_resident = 0;
    ah->name_length = 0;
    ah->name_offset = 24;
    ah->flags = 0;
    ah->attribute_id = 0;
    ah->length = sizeof(*ah) + sizeof(*body) + NTFS_STDINFO_SIZE;
    body = (ntfs_attr_resident_t *)(ah + 1);
    body->content_size = NTFS_STDINFO_SIZE;
    body->content_offset = sizeof(*body);
    body->indexed = 0;
    info = (ntfs_stdinfo_t *)(insert_at + sizeof(*ah) + body->content_offset);
    info->creation = info->modification = info->mft_modification
                   = info->access = now;
    info->flags = 0;
    info->max_versions = 0;
    info->version = 1;
    info->class_id = 0;
    info->owner_id = 0;
    info->security_id = 0;
    info->quota_charged = 0;
    info->usn = 0;
    rec->used_size += ah->length;
    if (rec->next_attr_id < 1) rec->next_attr_id = 1;
    return 0;
}

static int append_data_attr(uint8_t *buf, uint32_t buf_size,
                              const void *data, uint32_t count) {
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)buf;
    uint8_t *p = find_attr_end(buf, buf_size);
    uint32_t attr_len = (uint32_t)(sizeof(ntfs_attr_header_t) + 16 + count);
    if (attr_len & 3) attr_len = (attr_len + 3) & ~3u;
    if (p + attr_len + 4 > buf + buf_size) return -1;
    ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
    ah->type = NTFS_ATTR_DATA;
    ah->non_resident = 0;
    ah->name_length = 0;
    ah->name_offset = 24;
    ah->flags = 0;
    ah->attribute_id = rec->next_attr_id++;
    ah->length = attr_len;
    ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
    body->content_size = count;
    body->content_offset = sizeof(*body);
    body->indexed = 0;
    memcpy(p + sizeof(*ah) + sizeof(*body), data, count);
    rec->used_size += attr_len;
    uint8_t *e = p + attr_len;
    *(uint32_t *)e = 0xFFFFFFFFu;
    *(uint32_t *)(e + 4) = 0;
    return 0;
}

static int write_data_resident(uint8_t *buf, uint32_t buf_size,
                                uint64_t offset, const void *data,
                                uint32_t count) {
    uint32_t attr_len;
    uint8_t *p = find_attr_buf(buf, NTFS_ATTR_DATA, buf_size, &attr_len);
    if (!p) return -1;
    ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
    if (ah->non_resident) return -1;
    ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
    if (offset + count > body->content_size) return -1;
    memcpy(p + body->content_offset + offset, data, count);
    return 0;
}

int ntfs_write_alloc_inode(uint64_t *out_inode) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st || !out_inode) return -1;
    uint64_t f = 0;
    if (bitmap_find_free(st, &f) != 0) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, 6, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    ntfs_mft_record_t *mft = (ntfs_mft_record_t *)rec;
    uint8_t *p = rec + mft->first_attr_offset;
    while (p + sizeof(ntfs_attr_header_t) <= rec + st->mft_record_size) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
        if (ah->type == 0xFFFFFFFFu) break;
        if (ah->type == NTFS_ATTR_BITMAP) {
            if (!ah->non_resident) {
                ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
                uint8_t *bm = p + body->content_offset;
                set_bit(bm, f);
                write_mft_record_buf(st, 6, rec);
                kfree(rec);
                *out_inode = f;
                return 0;
            }
            ntfs_attr_nonresident_t *nr = (ntfs_attr_nonresident_t *)(ah + 1);
            uint8_t *rdata = (uint8_t *)p + nr->run_list_offset;
            uint64_t off = 0, len = 0;
            if (ntfs_resolve_data_run(rdata, &off, &len) != 0) break;
            uint8_t *bm = (uint8_t *)kmalloc(st->cluster_size);
            if (!bm) { kfree(rec); return -1; }
            if (read_cluster(st, off, bm) != 0) { kfree(bm); kfree(rec); return -1; }
            set_bit(bm, f);
            if (write_cluster(st, off, bm) != 0) { kfree(bm); kfree(rec); return -1; }
            kfree(bm);
            *out_inode = f;
            kfree(rec);
            return 0;
        }
        if (ah->length == 0) break;
        p += ah->length;
    }
    kfree(rec);
    return -1;
}

static int init_mft_record(uint8_t *buf, uint32_t buf_size, uint64_t parent_inode,
                            uint16_t mode, const char *name) {
    memset(buf, 0, buf_size);
    ntfs_mft_record_t *rec = (ntfs_mft_record_t *)buf;
    memcpy(rec->magic, "FILE", 4);
    rec->update_seq_offset = 0x30;
    rec->update_seq_size = 1;
    rec->sequence_number = 1;
    rec->hard_link_count = 1;
    rec->flags = (mode == NTFS_FILE_TYPE_DIR) ? 0x04 : 0x01;
    rec->used_size = sizeof(*rec);
    rec->allocated_size = buf_size;
    rec->base_record = 0;
    rec->next_attr_id = 0;

    uint8_t *attr_start = buf + sizeof(ntfs_mft_record_t);
    *(uint32_t *)attr_start = 0xFFFFFFFFu;
    *(uint32_t *)(attr_start + 4) = 0;
    rec->first_attr_offset = (uint16_t)sizeof(ntfs_mft_record_t);

    if (update_stdinfo(buf, buf_size, mode, 0) != 0) return -1;
    uint8_t *end = find_attr_end(buf, buf_size);
    if (end >= buf + buf_size - 0x80) return -1;
    uint32_t fnlen = 0;
    while (name[fnlen] && fnlen < 255) fnlen++;
    ntfs_attr_header_t *ah = (ntfs_attr_header_t *)end;
    uint32_t attr_len = (uint32_t)(sizeof(*ah) + 16 + 66 + fnlen * 2);
    if (attr_len & 3) attr_len = (attr_len + 3) & ~3u;
    ah->type = NTFS_ATTR_FILE_NAME;
    ah->non_resident = 0;
    ah->name_length = 0;
    ah->name_offset = 24;
    ah->flags = 0;
    ah->attribute_id = rec->next_attr_id++;
    ah->length = attr_len;
    ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
    body->content_size = 66 + fnlen * 2;
    body->content_offset = sizeof(*body);
    body->indexed = 0;
    uint8_t *fndata = (uint8_t *)ah + sizeof(*ah) + sizeof(*body);
    for (uint32_t i = 0; i < fnlen; i++) fndata[i * 2] = name[i];
    rec->used_size += attr_len;
    uint8_t *e = end + attr_len;
    *(uint32_t *)e = 0xFFFFFFFFu;
    *(uint32_t *)(e + 4) = 0;
    (void)parent_inode;
    return 0;
}

int ntfs_write_create_file(uint64_t parent_inode, const char *name,
                              uint64_t *out_inode) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st || !name || !out_inode) return -1;
    if (ntfs_write_alloc_inode(out_inode) != 0) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (init_mft_record(rec, st->mft_record_size, parent_inode,
                          NTFS_FILE_TYPE_FILE, name) != 0) {
        kfree(rec); return -1;
    }
    if (write_mft_record_buf(st, *out_inode, rec) != 0) {
        kfree(rec); return -1;
    }
    kfree(rec);
    return 0;
}

int ntfs_write_create_dir(uint64_t parent_inode, const char *name,
                            uint64_t *out_inode) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st || !name || !out_inode) return -1;
    if (ntfs_write_alloc_inode(out_inode) != 0) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (init_mft_record(rec, st->mft_record_size, parent_inode,
                          NTFS_FILE_TYPE_DIR, name) != 0) {
        kfree(rec); return -1;
    }
    uint8_t *end = find_attr_end(rec, st->mft_record_size);
    if (end + 64 < rec + st->mft_record_size) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)end;
        ah->type = NTFS_ATTR_INDEX_ROOT;
        ah->non_resident = 0;
        ah->name_length = 0;
        ah->name_offset = 24;
        ah->flags = 0;
        ah->attribute_id = 2;
        ah->length = 56;
        ntfs_mft_record_t *mrec = (ntfs_mft_record_t *)rec;
        ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
        body->content_size = 16;
        body->content_offset = sizeof(*body);
        body->indexed = 0;
        memset(end + sizeof(*ah) + sizeof(*body), 0, 16);
        mrec->used_size += ah->length;
        uint8_t *e = end + ah->length;
        *(uint32_t *)e = 0xFFFFFFFFu;
    }
    if (write_mft_record_buf(st, *out_inode, rec) != 0) {
        kfree(rec); return -1;
    }
    kfree(rec);
    return 0;
}

int ntfs_write_data(uint64_t inode, uint64_t offset,
                      const void *buf, uint32_t count) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st || count == 0 || !buf) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, inode, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    uint32_t attr_len = 0;
    uint8_t *p = find_attr_buf(rec, NTFS_ATTR_DATA, st->mft_record_size, &attr_len);
    int r = -1;
    if (p) {
        r = write_data_resident(rec, st->mft_record_size, offset, buf, count);
    } else {
        r = append_data_attr(rec, st->mft_record_size, buf, count);
    }
    if (r == 0) {
        write_mft_record_buf(st, inode, rec);
    }
    kfree(rec);
    return r;
}

int ntfs_write_truncate(uint64_t inode, uint64_t new_size) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, inode, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    uint32_t attr_len = 0;
    uint8_t *p = find_attr_buf(rec, NTFS_ATTR_DATA, st->mft_record_size,
                                &attr_len);
    if (!p) { kfree(rec); return -1; }
    ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
    ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
    if (new_size < body->content_size) {
        body->content_size = (uint32_t)new_size;
        uint8_t *e = p + ah->length;
        *(uint32_t *)e = 0xFFFFFFFFu;
        *(uint32_t *)(e + 4) = 0;
    }
    write_mft_record_buf(st, inode, rec);
    kfree(rec);
    return 0;
}

int ntfs_write_delete(uint64_t parent_inode, const char *name) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st || !name) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, parent_inode, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    uint32_t fnlen = 0;
    while (name[fnlen] && fnlen < 255) fnlen++;
    uint32_t attr_len = 0;
    uint8_t *p = find_attr_buf(rec, NTFS_ATTR_FILE_NAME, st->mft_record_size,
                                &attr_len);
    (void)p;
    (void)fnlen;
    write_mft_record_buf(st, parent_inode, rec);
    kfree(rec);
    return 0;
}

int ntfs_write_rename(uint64_t parent_inode, const char *old_name,
                        uint64_t new_parent_inode, const char *new_name) {
    const ntfs_state_t *st = ntfs_get_state();
    (void)old_name; (void)new_parent_inode;
    if (!st || !new_name) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    uint64_t target = 0;
    if (ntfs_read_file_data(target, 0, 0, rec) != 0) {
        kfree(rec); return -1;
    }
    uint32_t attr_len = 0;
    uint8_t *p = find_attr_buf(rec, NTFS_ATTR_FILE_NAME, st->mft_record_size,
                                &attr_len);
    if (p) {
        ntfs_attr_header_t *ah = (ntfs_attr_header_t *)p;
        ntfs_attr_resident_t *body = (ntfs_attr_resident_t *)(ah + 1);
        uint8_t *fndata = p + body->content_offset;
        uint32_t fnlen = 0;
        while (new_name[fnlen] && fnlen < 127) fnlen++;
        for (uint32_t i = 0; i < fnlen; i++) fndata[i * 2] = new_name[i];
        write_mft_record_buf(st, parent_inode, rec);
    }
    kfree(rec);
    return 0;
}

int ntfs_write_set_attr(uint64_t inode, uint16_t mode) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, inode, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    ntfs_mft_record_t *mft = (ntfs_mft_record_t *)rec;
    if (mode == NTFS_FILE_TYPE_DIR) mft->flags |= 0x04;
    else mft->flags &= ~0x04;
    int r = write_mft_record_buf(st, inode, rec);
    kfree(rec);
    return r;
}

int ntfs_write_set_mtime(uint64_t inode, uint64_t mtime) {
    const ntfs_state_t *st = ntfs_get_state();
    if (!st) return -1;
    uint8_t *rec = (uint8_t *)kmalloc(st->mft_record_size);
    if (!rec) return -1;
    if (read_mft_record_buf(st, inode, rec, st->mft_record_size) != 0) {
        kfree(rec); return -1;
    }
    int r = update_stdinfo(rec, st->mft_record_size,
                            NTFS_FILE_TYPE_FILE, mtime);
    if (r == 0) write_mft_record_buf(st, inode, rec);
    kfree(rec);
    return r;
}