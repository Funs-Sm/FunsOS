#ifndef CAPABILITY_H
#define CAPABILITY_H

#include "stdint.h"

/* Capability-Based Security (POSIX capabilities extended).
 *
 * This is the foundation of fine-grained privileged operation control.
 * It extends the existing UID/GID / cas model (kernel/permission.h)
 * by adding 41+ named capabilities, each representable in three sets:
 *   - Permitted (P)    : the maximum that can be gained
 *   - Inheritable (I)  : passed across exec()
 *   - Effective (E)    : currently active
 *
 * The bounding set restricts inherited capabilities even from root. Files
 * may also carry file capabilities, distinct from and combined with the
 * bits above. Threaded processes may have a separate ambient set (used
 * by library code without capability forwarding).
 */

#define CAP_MAX_BITS            64

/* Capability classes + numbers (subset of Linux). */
#define CAP_CHOWN               0
#define CAP_DAC_OVERRIDE        1
#define CAP_DAC_READ_SEARCH     2
#define CAP_FOWNER              3
#define CAP_FSETID              4
#define CAP_KILL                5
#define CAP_SETGID              6
#define CAP_SETUID              7
#define CAP_SETPCAP             8
#define CAP_LINUX_IMMUTABLE     9
#define CAP_NET_BIND_SERVICE    10
#define CAP_NET_BROADCAST       11
#define CAP_NET_ADMIN           12
#define CAP_NET_RAW             13
#define CAP_IPC_LOCK            14
#define CAP_IPC_OWNER           15
#define CAP_SYS_MODULE          16
#define CAP_SYS_RAWIO           17
#define CAP_SYS_CHROOT          18
#define CAP_SYS_PTRACE          19
#define CAP_SYS_PACCT           20
#define CAP_SYS_ADMIN           21
#define CAP_SYS_BOOT            22
#define CAP_SYS_NICE            23
#define CAP_SYS_RESOURCE        24
#define CAP_SYS_TIME            25
#define CAP_SYS_TTY_CONFIG      26
#define CAP_MKNOD               27
#define CAP_LEASE               28
#define CAP_AUDIT_WRITE         29
#define CAP_AUDIT_CONTROL       30
#define CAP_SETFCAP            31
#define CAP_MAC_OVERRIDE        32
#define CAP_MAC_ADMIN           33
#define CAP_SYSLOG              34
#define CAP_WAKE_ALARM          35
#define CAP_BLOCK_SUSPEND       36
#define CAP_AUDIT_READ          37
#define CAP_PERFMON            38
#define CAP_BPF                39
#define CAP_CHECKPOINT_RESTORE 40

#define CAP_NUM_CAPS            41

/* Per-credential capability layout (3 sets, each a bitmap). */
typedef struct {
    uint64_t permitted[1];   /* CAP_NUM_CAPS / 64 = 1 word */
    uint64_t inheritable[1];
    uint64_t effective[1];
    uint64_t bounding[1];    /* never increases, controls inheritance */
    uint64_t ambient[1];     /* auto-raised on exec if permitted&inheritable */
} cap_set_t;

/* Per-thread capability (subset of cred used by a single task). */
typedef struct {
    cap_set_t set;
    /* File capabilities attached to the program being executed. */
    uint64_t file_effective;
    uint64_t file_permitted;
    uint64_t file_inheritable;
} cap_t;

/* Per-credential bundle held by tasks and files. */
typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t suid;
    uint32_t sgid;
    uint32_t fsuid;
    uint32_t fsgid;
    cap_t    cap;
    /* Supplementary group list. */
    uint32_t groups[32];
    uint32_t n_groups;
    /* Security label (slot for LSM-style state). */
    uint8_t  label[32];
    uint32_t label_len;
    /* Reference counter. */
    uint32_t refcount;
} cred_t;

#define CAP_TO_BIT(n)           ((uint64_t)1 << (uint64_t)(n))

/* Public API */
int  cred_init(cred_t *cred, uint32_t uid, uint32_t gid);
int  cred_copy(cred_t *dst, const cred_t *src);
void cred_destroy(cred_t *cred);
cred_t *cred_get(cred_t *cred);

int  cap_set_bit(cap_set_t *set, int cap);
int  cap_clear_bit(cap_set_t *set, int cap);
int  cap_test_bit(const cap_set_t *set, int cap);
int  cap_test_effective(const cred_t *cred, int cap);
int  cap_test_permitted(const cred_t *cred, int cap);
void cap_raise(cap_set_t *set, int cap);
void cap_lower(cap_set_t *set, int cap);
int  cap_drop_all(cred_t *cred);

/* Permission checks for specific operations. */
int capable_raise(cap_set_t *set, int cap);
int cap_compare(const cap_set_t *a, const cap_set_t *b);

/* Capability-aware fork / exec transition. */
int cap_exec_transition(cred_t *new_cred, const cred_t *old_cred);

/* Initial capabilities for root / user. */
void cap_init_root(cred_t *cred);
void cap_init_user(cred_t *cred);

#endif