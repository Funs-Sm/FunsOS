#include "procfs.h"
#include "kheap.h"
#include "string.h"
#include "sched.h"
#include "process.h"
#include "pmm.h"
#include "stdio.h"
#include "version.h"
#include "smp.h"
#include "net.h"
#include "arp.h"
#include "dhcp.h"
#include "tcp.h"
#include "udp.h"
#include "vmm.h"
#include "klog.h"
#include "user.h"

static procfs_entry_t entries[64];
static uint32_t entry_count;

static dentry_t *procfs_root_dentry;
static inode_t *procfs_root_inode;
static superblock_t *procfs_sb;

/* ------------------------------------------------------------------ */
/*  /proc/cpuinfo                                                      */
/* ------------------------------------------------------------------ */

static int32_t cpuinfo_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    uint32_t cpu_cnt = smp_get_cpu_count();
    int32_t len = 0;

    for (uint32_t i = 0; i < cpu_cnt; i++) {
        len += sprintf(tmp + len,
            "processor\t: %u\n"
            "vendor_id\t: GenuineIntel\n"
            "model name\t: x86 Compatible CPU\n"
            "cpu MHz\t\t: unknown\n\n",
            i);
    }

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/meminfo                                                      */
/* ------------------------------------------------------------------ */

static int32_t meminfo_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    uint32_t total_kb = (pmm_get_total_pages() * PMM_PAGE_SIZE) / 1024;
    uint32_t free_kb  = (pmm_get_free_pages() * PMM_PAGE_SIZE) / 1024;
    uint32_t used_kb  = (pmm_get_used_pages() * PMM_PAGE_SIZE) / 1024;
    int32_t len = sprintf(tmp,
        "MemTotal:       %u kB\n"
        "MemFree:        %u kB\n"
        "MemUsed:        %u kB\n"
        "Buffers:               0 kB\n"
        "Cached:                0 kB\n",
        total_kb, free_kb, used_kb);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/version                                                      */
/* ------------------------------------------------------------------ */

static int32_t version_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[128];
    int32_t len = sprintf(tmp, "%s\n", KERNEL_STRING);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/uptime                                                       */
/* ------------------------------------------------------------------ */

static int32_t uptime_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[64];
    extern uint32_t timer_get_ticks(void);
    uint32_t secs = timer_get_ticks() / 100;
    int32_t len = sprintf(tmp, "%u.00 %u.00\n", secs, secs);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/loadavg                                                      */
/* ------------------------------------------------------------------ */

static int32_t loadavg_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[64];
    int32_t len = sprintf(tmp, "0.00 0.00 0.00 1/%u 0\n",
                          smp_get_cpu_count());

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/net/dev                                                      */
/* ------------------------------------------------------------------ */

static int32_t netdev_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[1024];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "Inter-|   Receive                                            "
        "  |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed "
        "multicast|bytes    packets errs drop fifo colls carrier compressed\n");

    uint32_t if_count = net_get_interface_count();
    for (uint32_t i = 0; i < if_count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;
        len += sprintf(tmp + len,
            "%6s: %u %u 0 0 0 0 0 0 %u %u 0 0 0 0 0\n",
            iface->name,
            iface->rx_bytes, iface->rx_packets,
            iface->tx_bytes, iface->tx_packets);
    }

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/partitions                                                   */
/* ------------------------------------------------------------------ */

static int32_t partitions_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "major minor  #blocks  name\n\n"
        "  3     0    %u hd0\n"
        "  3     1    %u hd0p1\n",
        8192, 8192);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/stat                                                          */
/* ------------------------------------------------------------------ */

static int32_t stat_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[1024];
    int32_t len = 0;
    const sched_global_stats_t *gs = sched_get_global_stats();

    len += sprintf(tmp + len, "cpu  %llu 0 %llu %llu 0 0 0 0 0 0\n",
        gs->user_ticks, gs->kernel_ticks, gs->idle_ticks);

    uint32_t cpu_cnt = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_cnt; i++) {
        len += sprintf(tmp + len, "cpu%u %llu 0 %llu %llu 0 0 0 0 0 0\n",
            i, gs->user_ticks / cpu_cnt, gs->kernel_ticks / cpu_cnt, gs->idle_ticks / cpu_cnt);
    }

    len += sprintf(tmp + len, "intr %llu\n", gs->total_context_switches);
    len += sprintf(tmp + len, "ctxt %llu\n", gs->total_context_switches);
    len += sprintf(tmp + len, "processes %llu\n", (uint64_t)gs->preempt_count + gs->yield_count);
    len += sprintf(tmp + len, "procs_running 1\n");
    len += sprintf(tmp + len, "procs_blocked 0\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/interrupts                                                    */
/* ------------------------------------------------------------------ */

static int32_t interrupts_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    len += sprintf(tmp + len, "         CPU0\n");
    for (int i = 0; i < 16; i++) {
        const char *name = "?";
        switch (i) {
            case 0: name = "timer"; break;
            case 1: name = "keyboard"; break;
            case 2: name = "cascade"; break;
            case 3: name = "serial2"; break;
            case 4: name = "serial1"; break;
            case 5: name = "parallel2"; break;
            case 6: name = "floppy"; break;
            case 7: name = "parallel1"; break;
            case 8: name = "rtc"; break;
            case 9: name = "acpi"; break;
            case 12: name = "mouse"; break;
            case 14: name = "ide0"; break;
            case 15: name = "ide1"; break;
            default: name = "reserved"; break;
        }
        len += sprintf(tmp + len, "  %3d: %8u    IO-APIC   %s\n",
            i, 0, name);
    }

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/cmdline                                                       */
/* ------------------------------------------------------------------ */

static int32_t cmdline_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[128];
    int32_t len = sprintf(tmp, "root=/dev/ram0 init=/bin/shell ro quiet\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/modules                                                        */
/* ------------------------------------------------------------------ */

static int32_t modules_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[256];
    int32_t len = sprintf(tmp,
        "vfat 16384 0 - Live 0x00000000\n"
        "ext4 64000 0 - Live 0x00000000\n"
        "tcp_ipv4 32768 0 - Live 0x00000000\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/net/tcp                                                       */
/* ------------------------------------------------------------------ */

static int32_t nettcp_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[1024];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
    len += sprintf(tmp + len,
        "   0: 00000000:0050 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 0 1 0\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/net/udp                                                       */
/* ------------------------------------------------------------------ */

static int32_t netudp_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
    len += sprintf(tmp + len,
        "   0: 00000000:0035 00000000:0000 07 00000000:00000000 00:00000000 00000000     0        0 0 1 0\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/mounts                                                        */
/* ------------------------------------------------------------------ */

static int32_t mounts_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "rootfs / rootfs rw 0 0\n"
        "/dev/ram0 / ext4 rw,relatime 0 0\n"
        "proc /proc proc rw,relatime 0 0\n"
        "sysfs /sys sysfs rw,relatime 0 0\n"
        "devtmpfs /dev devtmpfs rw,relatime 0 0\n"
        "tmpfs /tmp tmpfs rw,relatime 0 0\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/filesystems                                                   */
/* ------------------------------------------------------------------ */

static int32_t filesystems_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    len += sprintf(tmp + len,
        "nodev   sysfs\n"
        "nodev   rootfs\n"
        "nodev   ramfs\n"
        "nodev   tmpfs\n"
        "nodev   proc\n"
        "nodev   devtmpfs\n"
        "nodev   devpts\n"
        "        ext2\n"
        "        ext3\n"
        "        ext4\n"
        "        vfat\n"
        "        fat32\n"
        "        xfs\n"
        "        btrfs\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/swaps                                                         */
/* ------------------------------------------------------------------ */

static int32_t swaps_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[256];
    int32_t len = sprintf(tmp,
        "Filename\t\t\t\tType\t\tSize\t\tUsed\tPriority\n");

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/vmstat                                                        */
/* ------------------------------------------------------------------ */

static int32_t vmstat_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[512];
    int32_t len = 0;

    uint32_t total_pages = pmm_get_total_pages();
    uint32_t free_pages = pmm_get_free_pages();

    len += sprintf(tmp + len, "nr_free_pages %u\n", free_pages);
    len += sprintf(tmp + len, "nr_alloc_batch %u\n", 0);
    len += sprintf(tmp + len, "pgpgin %u\n", total_pages - free_pages);
    len += sprintf(tmp + len, "pgpgout %u\n", 0);
    len += sprintf(tmp + len, "pswpin %u\n", 0);
    len += sprintf(tmp + len, "pswpout %u\n", 0);
    len += sprintf(tmp + len, "pgfault %u\n", 0);
    len += sprintf(tmp + len, "pgmajfault %u\n", 0);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/schedstat                                                     */
/* ------------------------------------------------------------------ */

static int32_t schedstat_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[1024];
    int32_t len = 0;
    const sched_global_stats_t *gs = sched_get_global_stats();

    len += sprintf(tmp + len, "version 15\n");
    len += sprintf(tmp + len, "timestamp %llu\n", sched_get_tick_count());
    len += sprintf(tmp + len, "cpu0 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
        gs->total_ticks, gs->idle_ticks, gs->user_ticks, gs->kernel_ticks,
        (uint64_t)0, (uint64_t)0, (uint64_t)0, (uint64_t)0,
        gs->total_context_switches, gs->preempt_count);

    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  /proc/[pid]/status                                                 */
/* ------------------------------------------------------------------ */

static int32_t pid_status_read(char *buf, uint32_t offset, uint32_t count) {
    char tmp[128];
    pcb_t *proc = sched_get_current();
    int32_t len = sprintf(tmp, "Pid: %d\nState: %u\nName: %s\n", proc->pid, proc->state, proc->name);
    if (offset >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - offset;
    if (avail > count) avail = count;
    memcpy(buf, tmp + offset, avail);
    return (int32_t)avail;
}

/* ------------------------------------------------------------------ */
/*  Entry management                                                   */
/* ------------------------------------------------------------------ */

static void procfs_add_entry(const char *name, uint32_t mode, int32_t (*read_proc)(char *, uint32_t, uint32_t)) {
    if (entry_count >= 64) return;
    strncpy(entries[entry_count].name, name, 63);
    entries[entry_count].name[63] = '\0';
    entries[entry_count].ino = entry_count + 1;
    entries[entry_count].mode = mode;
    entries[entry_count].size = 0;
    entries[entry_count].read_proc = read_proc;
    entry_count++;
}

int32_t procfs_create_pid_entry(uint32_t pid) {
    if (entry_count >= 64) return -1;

    char name[64];
    sprintf(name, "%u/status", pid);

    procfs_add_entry(name, FILE_MODE_READ, pid_status_read);
    return 0;
}

int32_t procfs_init(void) {
    entry_count = 0;
    memset(entries, 0, sizeof(entries));

    procfs_add_entry("cpuinfo", FILE_MODE_READ, cpuinfo_read);
    procfs_add_entry("meminfo", FILE_MODE_READ, meminfo_read);
    procfs_add_entry("version", FILE_MODE_READ, version_read);
    procfs_add_entry("uptime", FILE_MODE_READ, uptime_read);
    procfs_add_entry("loadavg", FILE_MODE_READ, loadavg_read);
    procfs_add_entry("net/dev", FILE_MODE_READ, netdev_read);
    procfs_add_entry("partitions", FILE_MODE_READ, partitions_read);
    procfs_add_entry("stat", FILE_MODE_READ, stat_read);
    procfs_add_entry("interrupts", FILE_MODE_READ, interrupts_read);
    procfs_add_entry("cmdline", FILE_MODE_READ, cmdline_read);
    procfs_add_entry("modules", FILE_MODE_READ, modules_read);
    procfs_add_entry("net/tcp", FILE_MODE_READ, nettcp_read);
    procfs_add_entry("net/udp", FILE_MODE_READ, netudp_read);
    procfs_add_entry("mounts", FILE_MODE_READ, mounts_read);
    procfs_add_entry("filesystems", FILE_MODE_READ, filesystems_read);
    procfs_add_entry("swaps", FILE_MODE_READ, swaps_read);
    procfs_add_entry("vmstat", FILE_MODE_READ, vmstat_read);
    procfs_add_entry("schedstat", FILE_MODE_READ, schedstat_read);

    uint32_t pid;
    for (pid = 0; pid < PROCFS_MAX_PROCS; pid++) {
        pcb_t *proc = process_get_pcb((pid_t)pid);
        if (proc && proc->state != PROCESS_UNUSED) {
            procfs_create_pid_entry(pid);
        }
    }

    return 0;
}

int32_t procfs_mount(superblock_t *sb, void *data) {
    (void)data;

    procfs_root_inode = (inode_t *)kmalloc(sizeof(inode_t));
    if (!procfs_root_inode) return -1;
    memset(procfs_root_inode, 0, sizeof(inode_t));
    procfs_root_inode->ino = 0;
    procfs_root_inode->mode = FILE_MODE_READ | FILE_MODE_DIR;
    procfs_root_inode->sb = sb;

    procfs_root_dentry = (dentry_t *)kmalloc(sizeof(dentry_t));
    if (!procfs_root_dentry) {
        kfree(procfs_root_inode);
        return -1;
    }
    memset(procfs_root_dentry, 0, sizeof(dentry_t));
    strcpy(procfs_root_dentry->name, "/");
    procfs_root_dentry->inode = procfs_root_inode;

    sb->root = procfs_root_inode;
    sb->fs_type = 4;
    sb->block_size = 4096;
    procfs_sb = sb;

    uint32_t i;
    for (i = 0; i < entry_count; i++) {
        dentry_t *de = (dentry_t *)kmalloc(sizeof(dentry_t));
        if (!de) continue;
        memset(de, 0, sizeof(dentry_t));
        strncpy(de->name, entries[i].name, 255);
        de->name[255] = '\0';

        inode_t *in = (inode_t *)kmalloc(sizeof(inode_t));
        if (!in) {
            kfree(de);
            continue;
        }
        memset(in, 0, sizeof(inode_t));
        in->ino = entries[i].ino;
        in->mode = entries[i].mode;
        in->sb = sb;
        in->private_data = &entries[i];
        de->inode = in;

        de->parent = procfs_root_dentry;
        de->next_sibling = procfs_root_dentry->child;
        procfs_root_dentry->child = de;
    }

    return 0;
}

int32_t procfs_open(inode_t *inode, file_t *file) {
    if (!inode || !file) return -1;
    file->inode = inode;
    file->offset = 0;
    file->private_data = inode->private_data;
    return 0;
}

int32_t procfs_read(file_t *file, void *buf, uint32_t count) {
    if (!file || !buf) return -1;
    procfs_entry_t *entry = (procfs_entry_t *)file->private_data;
    if (!entry || !entry->read_proc) return -1;
    return entry->read_proc((char *)buf, file->offset, count);
}

int32_t procfs_readdir(file_t *file, void *buf, uint32_t count) {
    if (!file || !buf) return -1;
    uint32_t idx = file->offset;
    if (idx >= entry_count) return 0;

    procfs_entry_t *entry = &entries[idx];
    uint32_t name_len = strlen(entry->name);
    uint32_t entry_size = name_len + 1 + sizeof(uint32_t) * 2;
    if (entry_size > count) return -1;

    memcpy(buf, entry->name, name_len + 1);
    memcpy((uint8_t *)buf + name_len + 1, &entry->ino, sizeof(uint32_t));
    memcpy((uint8_t *)buf + name_len + 1 + sizeof(uint32_t), &entry->mode, sizeof(uint32_t));

    file->offset++;
    return (int32_t)entry_size;
}

int32_t procfs_stat(inode_t *inode, inode_t *stat) {
    if (!inode || !stat) return -1;
    procfs_entry_t *entry = (procfs_entry_t *)inode->private_data;
    if (!entry) return -1;
    stat->ino = entry->ino;
    stat->mode = entry->mode;
    stat->size = entry->size;
    return 0;
}
