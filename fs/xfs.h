#ifndef XFS_H
#define XFS_H

#include "vfs.h"

#define XFS_MAGIC  0x58465342ULL
#define XFS_AGF_MAGIC 0x58414746
#define XFS_AGI_MAGIC 0x58414749
#define XFS_AGFL_MAGIC 0x5841464C
#define XFS_INODE_MAGIC 0x494E

#define XFS_DINODE_FMT_LOCAL  1
#define XFS_DINODE_FMT_EXTENTS 2
#define XFS_DINODE_FMT_BTREE  3
#define XFS_DINODE_FMT_UUID   4
#define XFS_DINODE_FMT_DEV    5

#define XFS_DIR2_BLOCK_MAGIC  0x58443242
#define XFS_DIR2_LEAF_MAGIC   0x58443244
#define XFS_DIR2_FREE_MAGIC   0x58443246
#define XFS_DIR2_DATA_MAGIC   0x58443244

typedef struct {
    uint32_t magic;
    uint32_t blocksize;
    uint64_t dblocks;
    uint64_t rblocks;
    uint32_t agcount;
    uint32_t agsize;
    uint32_t sectsize;
    uint32_t inodesize;
    uint32_t inopblock;
    char     fname[12];
    uint64_t rootino;
    uint64_t rbmino;
    uint64_t rsumino;
    uint32_t rextsize;
    uint32_t imax_pct;
    uint64_t icount;
    uint64_t ifree;
    uint64_t fdblocks;
    uint32_t version;
    uint32_t flags;
    uint8_t  uuid[16];
    uint32_t dirblocklog;
    uint32_t logsectsize;
    uint32_t logsectlog;
    uint64_t logstart;
    uint32_t rootdir_resblk;
} xfs_superblock_t;

typedef struct {
    uint16_t magic;
    uint16_t mode;
    int32_t  nlink;
    uint32_t uid, gid;
    uint64_t size;
    uint64_t atime, mtime, ctime;
} xfs_inode_core_t;

typedef struct xfs_inode_info {
    uint64_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t nlink;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t format;
    uint32_t nextents;
    uint64_t extent_start;
    uint64_t extent_count;
} xfs_inode_info_t;

typedef struct xfs_dir_entry {
    uint64_t ino;
    uint8_t  type;
    uint32_t name_len;
    char     name[256];
    struct xfs_dir_entry *next;
} xfs_dir_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seqno;
    uint32_t length;
    uint32_t bnofirst;
    uint32_t bnocnt;
    uint32_t btreefirst;
    uint32_t btreecnt;
    uint32_t flfirst;
    uint32_t flcnt;
    uint32_t flblocks;
    uint32_t freeblks;
    uint32_t longest;
    uint32_t btreelevels;
    uint32_t fllevels;
    uint32_t freeseg;
    uint64_t rsvd;
} __attribute__((packed)) xfs_agf_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seqno;
    uint32_t length;
    uint32_t count;
    uint32_t root;
    uint32_t level;
    uint32_t freecount;
    uint32_t newino;
    uint32_t dirino;
    uint32_t unlinked;
    uint32_t ino_size;
    uint32_t pagino;
    uint32_t ino_alignment;
    uint32_t ag_max_inode;
} __attribute__((packed)) xfs_agi_t;

int xfs_init(void);
int xfs_mount_device(const char *path, uint32_t device_id);
int xfs_read_file(const char *path, void *buf, uint32_t size);
int xfs_list_dir(const char *path);

int xfs_read_superblock(xfs_superblock_t *sb);
int xfs_read_agf(uint32_t ag_index, xfs_agf_t *agf);
int xfs_read_agi(uint32_t ag_index, xfs_agi_t *agi);
int xfs_read_disk_inode(uint64_t ino, void *di_buf);
int xfs_decode_extent(const uint8_t *raw, uint64_t *startoff,
                      uint64_t *startblock, uint32_t *blockcount);
int xfs_read_extent_data(uint64_t ino, void *buf, uint64_t offset, uint32_t size);
int xfs_read_dir_entries(uint64_t dir_ino, xfs_dir_entry_t **head);
void xfs_free_dir_entries(xfs_dir_entry_t *head);

#endif
