#include "acl.h"
#include "string.h"
#include "kheap.h"

acl_t *acl_init(void) {
    acl_t *acl = (acl_t *)kmalloc(sizeof(acl_t));
    if (acl == NULL) {
        return NULL;
    }
    acl_create(acl);
    return acl;
}

void acl_free(acl_t *acl) {
    if (!acl) return;
    if (acl->entries) {
        kfree(acl->entries);
    }
    kfree(acl);
}

int acl_create(acl_t *acl) {
    if (!acl) return -1;
    memset(acl, 0, sizeof(acl_t));
    acl->owner_uid = 0;
    acl->group_gid = 0;
    acl->owner_perm = PERM_READ | PERM_WRITE | PERM_EXEC;
    acl->group_perm = PERM_READ | PERM_EXEC;
    acl->other_perm = PERM_READ | PERM_EXEC;
    acl->entries = NULL;
    acl->entry_count = 0;
    return 0;
}

int acl_add_entry(acl_t *acl, uint32_t uid, uint32_t gid, uint16_t perms) {
    if (!acl) return -1;
    if (acl->entry_count >= ACL_MAX_ENTRIES) return -1;

    if (acl->entries == NULL) {
        acl->entries = (acl_entry_t *)kmalloc(sizeof(acl_entry_t) * ACL_MAX_ENTRIES);
        if (!acl->entries) return -1;
        memset(acl->entries, 0, sizeof(acl_entry_t) * ACL_MAX_ENTRIES);
    }

    acl_entry_t *e = &acl->entries[acl->entry_count];
    e->uid = uid;
    e->gid = gid;
    e->perms = (uint8_t)(perms & 0x7);
    acl->entry_count++;
    return 0;
}

int acl_remove_entry(acl_t *acl, uint32_t index) {
    if (!acl) return -1;
    if (index >= acl->entry_count) return -1;

    uint32_t i;
    for (i = index; i < acl->entry_count - 1; i++) {
        acl->entries[i] = acl->entries[i + 1];
    }
    acl->entry_count--;
    memset(&acl->entries[acl->entry_count], 0, sizeof(acl_entry_t));
    return 0;
}

int acl_check(acl_t *acl, uint32_t uid, uint32_t gid, uint8_t perm) {
    if (acl == NULL) {
        return 0;
    }

    uint8_t stored_perm;

    if (uid == acl->owner_uid) {
        stored_perm = acl->owner_perm;
    } else if (gid == acl->group_gid) {
        stored_perm = acl->group_perm;
    } else {
        stored_perm = acl->other_perm;
    }

    if ((perm & stored_perm) == perm) {
        return 1;
    }

    if (acl->entries && acl->entry_count > 0) {
        for (uint32_t i = 0; i < acl->entry_count; i++) {
            acl_entry_t *e = &acl->entries[i];
            int uid_match = (e->uid == (uint32_t)-1) || (e->uid == uid);
            int gid_match = (e->gid == (uint32_t)-1) || (e->gid == gid);
            if (uid_match && gid_match) {
                if ((perm & e->perms) == perm) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

int acl_check_access(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required) {
    return acl_check((acl_t *)acl, uid, gid, (uint8_t)required);
}

void acl_clear(acl_t *acl) {
    if (!acl) return;
    if (acl->entries) {
        kfree(acl->entries);
    }
    memset(acl, 0, sizeof(acl_t));
}
