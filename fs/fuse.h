#ifndef FUSE_H
#define FUSE_H

#include "vfs.h"
#include "stdint.h"

#define FUSE_LOOKUP    1
#define FUSE_FORGET    2
#define FUSE_GETATTR   3
#define FUSE_SETATTR   4
#define FUSE_READLINK  5
#define FUSE_SYMLINK   6
#define FUSE_MKNOD     8
#define FUSE_MKDIR     9
#define FUSE_UNLINK   10
#define FUSE_RMDIR    11
#define FUSE_RENAME   12
#define FUSE_OPEN     14
#define FUSE_READ     15
#define FUSE_WRITE    16
#define FUSE_RELEASE  18
#define FUSE_FSYNC    20
#define FUSE_INIT     26
#define FUSE_OPENDIR  27
#define FUSE_READDIR  28
#define FUSE_STATFS   29

#define FUSE_VERSION_MAJOR  7
#define FUSE_VERSION_MINOR  23

#define FUSE_MAX_REQ_QUEUE  64
#define FUSE_MAX_RESP_QUEUE 64

#define FUSE_DEV_MAJOR  10
#define FUSE_DEV_MINOR  229

typedef struct {
    uint32_t unique;
    uint32_t opcode;
    uint64_t nodeid;
    uint32_t uid, gid;
    uint32_t pid;
    uint32_t data_len;
    char     data[4096];
} fuse_request_t;

typedef struct {
    uint32_t unique;
    int32_t  error;
    uint32_t data_len;
    char     data[4096];
} fuse_response_t;

typedef struct fuse_req_node {
    fuse_request_t req;
    struct fuse_req_node *next;
} fuse_req_node_t;

typedef struct fuse_resp_node {
    fuse_response_t resp;
    struct fuse_resp_node *next;
} fuse_resp_node_t;

typedef struct fuse_mount {
    char mount_point[256];
    char daemon_path[256];
    superblock_t *sb;
    uint32_t next_unique;
    int active;
    int initialized;
    uint32_t proto_major;
    uint32_t proto_minor;
    uint32_t max_readahead;
    uint32_t flags;
    fuse_req_node_t *req_queue_head;
    fuse_req_node_t *req_queue_tail;
    uint32_t req_queue_count;
    fuse_resp_node_t *resp_queue_head;
    fuse_resp_node_t *resp_queue_tail;
    uint32_t resp_queue_count;
    struct fuse_mount *next;
} fuse_mount_t;

int fuse_register_fs(const char *mount_point, const char *daemon_path);
int fuse_unregister_fs(const char *mount_point);

int fuse_send_request(fuse_request_t *req, fuse_response_t *resp);
int fuse_enqueue_request(fuse_mount_t *fm, fuse_request_t *req);
int fuse_dequeue_response(fuse_mount_t *fm, fuse_response_t *resp);
int fuse_enqueue_response(fuse_mount_t *fm, fuse_response_t *resp);
int fuse_dequeue_request(fuse_mount_t *fm, fuse_request_t *req);

int fuse_mount(superblock_t *sb, void *data);
int fuse_dev_read(file_t *file, void *buf, uint32_t count);
int fuse_dev_write(file_t *file, const void *buf, uint32_t count);
int fuse_dev_ioctl(file_t *file, uint32_t cmd, void *arg);

fuse_mount_t *fuse_find_mount(const char *mount_point);

void fuse_init(void);

#endif
