#ifndef ACL_H
#define ACL_H

#include "stdint.h"
#include "permission.h"

acl_t *acl_init(void);
int acl_create(acl_t *acl);

int acl_add_entry(acl_t *acl, uint32_t uid, uint32_t gid, uint16_t perms);
int acl_remove_entry(acl_t *acl, uint32_t index);

int acl_check(acl_t *acl, uint32_t uid, uint32_t gid, uint8_t perm);
int acl_check_access(const acl_t *acl, uint32_t uid, uint32_t gid, uint16_t required);

void acl_clear(acl_t *acl);
void acl_free(acl_t *acl);

#endif
