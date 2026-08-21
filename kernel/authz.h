#ifndef AUTHZ_H
#define AUTHZ_H

#include "stdint.h"
#include "capability.h"
#include "mac_label.h"
#include "security_hook.h"

/* Integrated authorization layer.
 *
 * Performs a layered check combining:
 *   1. DAC (kernel/permission.h, basic UNIX mode bits)
 *   2. Capability check
 *   3. MAC label verification
 *   4. LSM hook chain
 *
 * Each check is independent; all must pass for the operation to be
 * authorized. Returns 0 if authorized, negative on denial.
 */

#define AUTHZ_REQ_READ   0x04
#define AUTHZ_REQ_WRITE  0x02
#define AUTHZ_REQ_EXEC   0x01

typedef struct {
    const cred_t    *cred;
    const mac_label_t *mac_subj;
    uint32_t file_uid;
    uint32_t file_gid;
    uint16_t file_mode;
    const mac_label_t *mac_obj;
    const char *path;
    int cap_required;        /* -1 = no specific cap */
    int require_effective;
} authz_request_t;

int authz_check_file(const authz_request_t *r);
int authz_check_cap(const cred_t *cred, int cap);
int authz_check_kill(const cred_t *cred);
int authz_check_mount(const cred_t *cred, const char *target);
int authz_check_bpf(const cred_t *cred);
int authz_audit_log(const authz_request_t *r, int decision);

/* Convenience wrappers. */
int authz_may_read(const cred_t *cred, const char *path,
                    uint32_t file_uid, uint16_t file_mode);
int authz_may_write(const cred_t *cred, const char *path,
                     uint32_t file_uid, uint16_t file_mode);
int authz_may_exec(const cred_t *cred, const char *path,
                    uint32_t file_uid, uint16_t file_mode);

#endif