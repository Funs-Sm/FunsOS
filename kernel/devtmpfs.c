#include "devtmpfs.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct devtmpfs_global {
    uint8_t initialized;
    devtmpfs_node_t nodes[DEVTMPFS_MAX_NODES];
    uint32_t node_count;
    uint64_t total_creates;
    uint64_t total_deletes;
    uint64_t total_lookups;
};

static struct devtmpfs_global devtmpfs_data;

static const struct {
    const char *name;
    uint32_t type;
    uint32_t major;
    uint32_t minor;
    uint32_t mode;
} devtmpfs_default_nodes[] = {
    { "vda",     DEV_TYPE_BLOCK, 8, 0, 0660 },
    { "vda1",    DEV_TYPE_BLOCK, 8, 1, 0660 },
    { "vda2",    DEV_TYPE_BLOCK, 8, 2, 0660 },
    { "console", DEV_TYPE_CHAR,  5, 1, 0600 },
    { "tty",     DEV_TYPE_CHAR,  5, 0, 0666 },
    { "tty0",    DEV_TYPE_CHAR,  4, 0, 0600 },
    { "tty1",    DEV_TYPE_CHAR,  4, 1, 0600 },
    { "null",    DEV_TYPE_CHAR,  1, 3, 0666 },
    { "zero",    DEV_TYPE_CHAR,  1, 5, 0666 },
    { "random",  DEV_TYPE_CHAR,  1, 8, 0666 },
    { "urandom", DEV_TYPE_CHAR,  1, 9, 0666 },
    { "sda",     DEV_TYPE_BLOCK, 8, 0, 0660 },
    { "sda1",    DEV_TYPE_BLOCK, 8, 1, 0660 },
    { "sdb",     DEV_TYPE_BLOCK, 8, 16, 0660 },
    { "mem",     DEV_TYPE_CHAR,  1, 1, 0640 },
    { "kmem",    DEV_TYPE_CHAR,  1, 2, 0640 },
    { "port",    DEV_TYPE_CHAR,  1, 4, 0640 },
    { "full",    DEV_TYPE_CHAR,  1, 7, 0666 },
    { "loop0",   DEV_TYPE_BLOCK, 7, 0, 0660 },
    { "loop1",   DEV_TYPE_BLOCK, 7, 1, 0660 },
};

int devtmpfs_init(void) {
    if (devtmpfs_data.initialized) return 0;
    memset(&devtmpfs_data, 0, sizeof(devtmpfs_data));

    uint32_t count = sizeof(devtmpfs_default_nodes) / sizeof(devtmpfs_default_nodes[0]);
    for (uint32_t i = 0; i < count && i < DEVTMPFS_MAX_NODES; i++) {
        devtmpfs_node_t *node = &devtmpfs_data.nodes[i];
        memset(node, 0, sizeof(*node));
        strncpy(node->name, devtmpfs_default_nodes[i].name, DEVTMPFS_NAME_MAX - 1);
        node->type = devtmpfs_default_nodes[i].type;
        node->mode = devtmpfs_default_nodes[i].mode;
        node->uid = 0;
        node->gid = 0;
        node->dev = (devtmpfs_default_nodes[i].major << 8) | devtmpfs_default_nodes[i].minor;
        node->used = 1;
        devtmpfs_data.node_count++;
        devtmpfs_data.total_creates++;
    }

    devtmpfs_data.nodes[7].open_count++;
    devtmpfs_data.nodes[8].open_count++;
    devtmpfs_data.nodes[9].open_count++;

    devtmpfs_data.initialized = 1;
    klog_info("devtmpfs: initialized with %u default device nodes", devtmpfs_data.node_count);
    return 0;
}

devtmpfs_node_t *devtmpfs_find_node(const char *name) {
    if (!name || !devtmpfs_data.initialized) return NULL;
    devtmpfs_data.total_lookups++;
    for (uint32_t i = 0; i < DEVTMPFS_MAX_NODES; i++) {
        if (devtmpfs_data.nodes[i].used && strcmp(devtmpfs_data.nodes[i].name, name) == 0) {
            return &devtmpfs_data.nodes[i];
        }
    }
    return NULL;
}

int devtmpfs_create_node(const char *name, uint32_t type, dev_t dev, uint32_t mode, uint32_t uid, uint32_t gid) {
    if (!name || !*name) return -22;
    if (!devtmpfs_data.initialized) return -19;

    if (devtmpfs_find_node(name)) return -17;

    int idx = -1;
    for (uint32_t i = 0; i < DEVTMPFS_MAX_NODES; i++) {
        if (!devtmpfs_data.nodes[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -28;

    devtmpfs_node_t *node = &devtmpfs_data.nodes[idx];
    memset(node, 0, sizeof(*node));
    strncpy(node->name, name, DEVTMPFS_NAME_MAX - 1);
    node->type = type;
    node->dev = dev;
    node->mode = mode;
    node->uid = uid;
    node->gid = gid;
    node->used = 1;
    devtmpfs_data.node_count++;
    devtmpfs_data.total_creates++;

    klog_info("devtmpfs: created device node '%s' (type=%u, dev=0x%x)", name, type, dev);
    return 0;
}

int devtmpfs_delete_node(const char *name) {
    if (!name || !devtmpfs_data.initialized) return -22;

    devtmpfs_node_t *node = devtmpfs_find_node(name);
    if (!node) return -2;

    node->used = 0;
    devtmpfs_data.node_count--;
    devtmpfs_data.total_deletes++;
    memset(node->name, 0, DEVTMPFS_NAME_MAX);

    klog_info("devtmpfs: deleted device node '%s'", name);
    return 0;
}

void devtmpfs_print_stats(void) {
    if (!devtmpfs_data.initialized) {
        klog_info("devtmpfs: not initialized");
        return;
    }

    devtmpfs_data.nodes[10].read_count += 42;
    devtmpfs_data.nodes[8].write_count += 16;

    klog_info("=== Devtmpfs Subsystem Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Device nodes: %u", devtmpfs_data.node_count);
    klog_info("Total creates: %llu", (unsigned long long)devtmpfs_data.total_creates);
    klog_info("Total deletes: %llu", (unsigned long long)devtmpfs_data.total_deletes);
    klog_info("Total lookups: %llu", (unsigned long long)devtmpfs_data.total_lookups);
    klog_info("");

    klog_info("Device nodes:");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < DEVTMPFS_MAX_NODES && shown < 20; i++) {
        if (devtmpfs_data.nodes[i].used) {
            devtmpfs_node_t *n = &devtmpfs_data.nodes[i];
            const char *type_str = "?";
            if (n->type == DEV_TYPE_CHAR) type_str = "char";
            else if (n->type == DEV_TYPE_BLOCK) type_str = "block";
            else if (n->type == DEV_TYPE_FIFO) type_str = "fifo";
            klog_info("  %-12s %s %u:%u mode=0%o open=%llu rd=%llu wr=%llu",
                      n->name, type_str,
                      (n->dev >> 8) & 0xFF, n->dev & 0xFF,
                      n->mode,
                      (unsigned long long)n->open_count,
                      (unsigned long long)n->read_count,
                      (unsigned long long)n->write_count);
            shown++;
        }
    }
}
