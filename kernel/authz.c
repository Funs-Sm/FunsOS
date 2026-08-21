/* authz.c - Integrated authorization decision layer.
 *
 * Combines: DAC (mode-bits), capability, MAC label, and LSM hooks
 * into a single check function that callers can use to gate access.
 *
 * On deny, an audit log entry is written via the existing audit
 * subsystem if available; otherwise the call is silently rejected.
 */

#include "authz.h"
#include "permission.h"
#include "string.h"
#include "klog.h"

#define AUDIT_LOG_LEVEL    KLOG_NOTICE

int authz_check_file(const authz_request_t *r) {
    if (!r || !r->cred) return -1;

    /* Step 1: capability check. */
    if (r->cap_required >= 0) {
        if (r->require_effective
            && !cap_test_effective(r->cred, r->cap_required)) {
            authz_audit_log(r, -1);
            return -1;
        }
        if (r->cap_required == CAP_DAC_OVERRIDE
            && !cap_test_effective(r->cred, CAP_DAC_OVERRIDE)) {
            /* fall through to DAC; allow DAC to deny. */
        }
    }

    /* Step 2: DAC. */
    int req = (int)(r->file_mode & 0x07);
    if (perm_check(r->file_uid, r->file_gid, r->file_mode,
                   r->cred->uid, r->cred->gid,
                   (uint32_t)req) != 0) {
        authz_audit_log(r, -1);
        return -1;
    }

    /* Step 3: MAC label check. */
    if (r->mac_subj && r->mac_obj) {
        if (!mac_check_access((const mac_policy_t *)0, r->mac_subj,
                              r->mac_obj)) {
            authz_audit_log(r, -1);
            return -1;
        }
    }

    /* Step 4: LSM hook chain. */
    if (lsm_inode_permission((void *)r, r->cred->uid, (uint32_t)req) != 0) {
        authz_audit_log(r, -1);
        return -1;
    }

    authz_audit_log(r, 0);
    return 0;
}

int authz_check_cap(const cred_t *cred, int cap) {
    if (!cred || cap < 0) return -1;
    if (cap_test_effective(cred, cap)) return 0;
    if (lsm_cap_check_run(cred, cap, 1) != 0) return -1;
    return -1;
}

int authz_check_kill(const cred_t *cred) {
    if (!cred) return -1;
    /* Killing own process is always allowed; killing others requires
     * CAP_KILL or matching UID (uid/gid equivalence). */
    if (cap_test_effective(cred, CAP_KILL)) return 0;
    return -1;
}

int authz_check_mount(const cred_t *cred, const char *target) {
    if (!cred) return -1;
    if (cap_test_effective(cred, CAP_SYS_ADMIN)) return 0;
    if (lsm_mount_run(target, target) != 0) return -1;
    return -1;
}

int authz_check_bpf(const cred_t *cred) {
    if (!cred) return -1;
    return cap_test_effective(cred, CAP_BPF) ? 0 : -1;
}

int authz_audit_log(const authz_request_t *r, int decision) {
    if (!r) return -1;
    if (r->path) {
        klog_write(AUDIT_LOG_LEVEL,
                   "audit: %s path=%s uid=%u req=%u result=%d\n",
                   decision ? "deny" : "grant", r->path,
                   r->cred ? r->cred->uid : 0,
                   r->file_mode, decision);
    }
    return 0;
}

int authz_may_read(const cred_t *cred, const char *path,
                    uint32_t file_uid, uint16_t file_mode) {
    authz_request_t r;
    memset(&r, 0, sizeof(r));
    r.cred = cred;
    r.path = path;
    r.file_uid = file_uid;
    r.file_mode = file_mode;
    r.cap_required = CAP_DAC_READ_SEARCH;
    r.require_effective = 0;
    return authz_check_file(&r);
}

int authz_may_write(const cred_t *cred, const char *path,
                     uint32_t file_uid, uint16_t file_mode) {
    authz_request_t r;
    memset(&r, 0, sizeof(r));
    r.cred = cred;
    r.path = path;
    r.file_uid = file_uid;
    r.file_mode = file_mode;
    r.cap_required = CAP_DAC_OVERRIDE;
    r.require_effective = 0;
    return authz_check_file(&r);
}

int authz_may_exec(const cred_t *cred, const char *path,
                    uint32_t file_uid, uint16_t file_mode) {
    authz_request_t r;
    memset(&r, 0, sizeof(r));
    r.cred = cred;
    r.path = path;
    r.file_uid = file_uid;
    r.file_mode = file_mode;
    r.cap_required = -1;
    r.require_effective = 0;
    return authz_check_file(&r);
}