#ifndef TARFS_H
#define TARFS_H

#include "vfs.h"

/* POSIX tar header (512 bytes) */
typedef struct posix_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} posix_header_t;

/* Mount-time information: pointer to the in-memory tar archive */
typedef struct tarfs_mount_info {
    void *archive_data;
    uint32_t archive_size;
} tarfs_mount_info_t;

/* Internal node structure (exposed for VFS readdir support) */
#define TARFS_MAX_NAME 255

typedef struct tarfs_node {
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t data_offset;
    struct tarfs_node *parent;
    struct tarfs_node *child;
    struct tarfs_node *next_sibling;
    char name[TARFS_MAX_NAME + 1];
    uint32_t nlinks;
    uint32_t mtime;
} tarfs_node_t;

int32_t tarfs_init(void);
int32_t tarfs_mount(superblock_t *sb, void *data);

#endif
