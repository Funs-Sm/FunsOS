#include "acl.h"
#include "string.h"

int acl_create(acl_t *acl) {
    if (!acl) return -1;
    memset(acl, 0, sizeof(acl_t));
    acl->count = 0;
    return 0;
}

int acl_add_entry(acl_t *acl, uint32_t uid, uint32_t gid, uint16_t perms) {
    if (!acl) return -1;
    if (acl->count >= ACL_MAX_ENTRIES) return -1;

    acl_entry_t *e = &acl->entries[acl->count];
    e->uid = uid;
    e->gid = gid;
    e->perms = perms;
    acl->count++;
    return 0;
}

int acl_remove_entry(acl_t *acl, uint32_t index) {
    if (!acl) return -1;
    if (index >= acl->count) return -1;

    /* 将后续条目前移覆盖被删条目 */
    uint32_t i;
    for (i = index; i < acl->count - 1; i++) {
        acl->entries[i] = acl->entries[i + 1];
    }
    acl->count--;
    /* 清空最后一条以避免脏数据 */
    memset(&acl->entries[acl->count], 0, sizeof(acl_entry_t));
    return 0;
}

int acl_check_access(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required) {
    return acl_check(acl, uid, gid, required);
}

void acl_clear(acl_t *acl) {
    if (!acl) return;
    memset(acl, 0, sizeof(acl_t));
}
