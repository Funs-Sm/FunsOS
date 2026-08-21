/* capability.c - Capability based authorization (POSIX-style).
 *
 * Implements the three capability bit sets (P/E/I) plus bounding and
 * ambient for finer authorization than UID==0 bypass. Provides routines
 * for capability transitions across fork() and exec().
 */

#include "capability.h"
#include "string.h"

#define BIT_TEST(w, b)  ((w)[(b) >> 6] & (1ULL << ((b) & 63)))
#define BIT_SET(w, b)   ((w)[(b) >> 6] |= (1ULL << ((b) & 63)))
#define BIT_CLR(w, b)   ((w)[(b) >> 6] &= ~(1ULL << ((b) & 63)))

static uint32_t g_caps_version = 1;       /* bumped on breaking ABI change */

int cred_init(cred_t *cred, uint32_t uid, uint32_t gid) {
    if (!cred) return -1;
    memset(cred, 0, sizeof(*cred));
    cred->uid = uid;
    cred->gid = gid;
    cred->suid = uid;
    cred->sgid = gid;
    cred->fsuid = uid;
    cred->fsgid = gid;
    cred->refcount = 1;
    cred->label_len = 0;
    if (uid == 0) cap_init_root(cred);
    else cap_init_user(cred);
    return 0;
}

void cred_destroy(cred_t *cred) {
    if (!cred) return;
    if (cred->refcount > 0) cred->refcount--;
    cred = (cred_t *)0;  /* invalidated */
}

cred_t *cred_get(cred_t *cred) {
    if (!cred) return (cred_t *)0;
    cred->refcount++;
    return cred;
}

int cred_copy(cred_t *dst, const cred_t *src) {
    if (!dst || !src) return -1;
    memcpy(dst, src, sizeof(*dst));
    dst->refcount = 1;
    return 0;
}

int cap_set_bit(cap_set_t *set, int cap) {
    if (!set || cap < 0 || cap >= CAP_NUM_CAPS) return -1;
    BIT_SET(set->permitted, cap);
    BIT_SET(set->effective, cap);
    return 0;
}

int cap_clear_bit(cap_set_t *set, int cap) {
    if (!set || cap < 0 || cap >= CAP_NUM_CAPS) return -1;
    BIT_CLR(set->permitted, cap);
    BIT_CLR(set->effective, cap);
    BIT_CLR(set->inheritable, cap);
    return 0;
}

int cap_test_bit(const cap_set_t *set, int cap) {
    if (!set || cap < 0 || cap >= CAP_NUM_CAPS) return 0;
    return (int)BIT_TEST(set->effective, cap);
}

int cap_test_effective(const cred_t *cred, int cap) {
    if (!cred) return 0;
    return cap_test_bit(&cred->cap.set, cap);
}

int cap_test_permitted(const cred_t *cred, int cap) {
    if (!cred || cap < 0 || cap >= CAP_NUM_CAPS) return 0;
    return (int)BIT_TEST(cred->cap.set.permitted, cap);
}

void cap_raise(cap_set_t *set, int cap) { cap_set_bit(set, cap); }
void cap_lower(cap_set_t *set, int cap) { cap_clear_bit(set, cap); }

int cap_drop_all(cred_t *cred) {
    if (!cred) return -1;
    memset(&cred->cap.set, 0, sizeof(cred->cap.set));
    return 0;
}

int capable_raise(cap_set_t *set, int cap) {
    if (!set || cap < 0 || cap >= CAP_NUM_CAPS) return -1;
    BIT_SET(set->bounding, cap);
    BIT_SET(set->permitted, cap);
    BIT_SET(set->effective, cap);
    return 0;
}

int cap_compare(const cap_set_t *a, const cap_set_t *b) {
    if (!a || !b) return -1;
    return memcmp(a, b, sizeof(*a));
}

/* Compute the new capability set after exec():
 *   P' = file_p | (file_i & bounding) | (P & ~file_p & ~i_of_inh)
 *   E' = file_e ? file_p : P'
 *   I' = file_i | (I & bounding)
 *   Ambient is not modified here (separate path).
 */
int cap_exec_transition(cred_t *new_cred, const cred_t *old_cred) {
    if (!new_cred || !old_cred) return -1;
    cap_set_t old = old_cred->cap.set;
    cap_set_t file = new_cred->cap.set;  /* pretends to have file caps */
    file.effective[0] = new_cred->cap.file_effective;
    file.permitted[0] = new_cred->cap.file_permitted;
    file.inheritable[0] = new_cred->cap.file_inheritable;

    cap_set_t n;
    /* Bounding intersect: inheritable file caps must fit in bounding. */
    n.inheritable[0] = file.inheritable[0] & old.bounding[0];
    n.inheritable[0] |= old.inheritable[0];

    n.permitted[0] = file.permitted[0]
                   | (file.inheritable[0] & old.bounding[0])
                   | (old.permitted[0] & ~(file.permitted[0]
                                            | old.inheritable[0]));
    n.effective[0] = file.effective[0] ? file.permitted[0] : n.permitted[0];
    n.bounding[0]  = old.bounding[0];
    n.ambient[0]   = old.ambient[0];

    new_cred->cap.set = n;
    return 0;
}

/* Initial capabilities for a root user. */
void cap_init_root(cred_t *cred) {
    if (!cred) return;
    /* full bounding for backwards compat with UID-based calls */
    memset(&cred->cap.set, 0xFF, sizeof(cred->cap.set));
    /* keep CAP set within current supported number */
    uint64_t beyond = ~((1ULL << CAP_NUM_CAPS) - 1);
    cred->cap.set.permitted[0] &= ~beyond;
    cred->cap.set.effective[0] &= ~beyond;
    cred->cap.set.inheritable[0] = 0;
    cred->cap.set.bounding[0] = (1ULL << CAP_NUM_CAPS) - 1;
    cred->cap.set.ambient[0] = 0;
    (void)g_caps_version;
}

void cap_init_user(cred_t *cred) {
    if (!cred) return;
    memset(&cred->cap.set, 0, sizeof(cred->cap.set));
    /* Ordinary users get CAP_KILL, CAP_NET_RAW only by default. */
    cred->cap.set.permitted[0] = 0;
    cred->cap.set.effective[0] = 0;
    cred->cap.set.inheritable[0] = 0;
    cred->cap.set.bounding[0] = 0;
    cred->cap.set.ambient[0] = 0;
}