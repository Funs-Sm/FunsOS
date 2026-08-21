#ifndef MAC_LABEL_H
#define MAC_LABEL_H

#include "stdint.h"

/* Mandatory Access Control labels and policy engine.
 *
 * Complements capability/DAC by attaching a context label to each
 * subject (task) and object (file/socket/...). A static policy table
 * defines allowed subject -> object transitions.
 *
 * The policy is intentionally tiny and static - it serves to demonstrate
 * the policy verification path. For full MLS-style enforcement, replace
 * the static table with an external policy blob.
 */

#define MAC_LABEL_LEN          32
#define MAC_OBJ_MAX_SUBJECTS  8

/* Built-in sensitivity levels (MLS style). */
#define MAC_LEVEL_UNCLASSED    1
#define MAC_LEVEL_CONFIDENTIAL 2
#define MAC_LEVEL_SECRET       3
#define MAC_LEVEL_TOPSECRET    4

/* Categories (8 bits per subject). */
#define MAC_CAT_FILE     (1 << 0)
#define MAC_CAT_NET      (1 << 1)
#define MAC_CAT_PROC     (1 << 2)
#define MAC_CAT_KERNEL   (1 << 3)
#define MAC_CAT_USER     (1 << 4)
#define MAC_CAT_DEVICE   (1 << 5)

typedef struct {
    char     name[MAC_LABEL_LEN + 1];
    uint32_t sensitivity;       /* 0..N, monotonic numeric level */
    uint32_t categories;        /* bitfield */
    uint8_t  reserved[3];
} mac_label_t;

/* Allowed domain transitions:
 *   <subj,obj> allowed when subscriber's category covers obj.category.
 */
typedef struct {
    mac_label_t subjects[MAC_OBJ_MAX_SUBJECTS];
    int         n_subjects;
    mac_label_t objects[MAC_OBJ_MAX_SUBJECTS];
    int         n_objects;
} mac_class_t;

typedef struct {
    mac_class_t classes[16];
    int         n_classes;
    uint64_t    denied;
    uint64_t    granted;
} mac_policy_t;

int  mac_init(mac_policy_t *p);
int  mac_add_label(mac_policy_t *p, const char *name, uint32_t sens,
                   uint32_t cats);
int  mac_check_access(const mac_policy_t *p, const mac_label_t *s,
                      const mac_label_t *o);
int  mac_check_read(const mac_policy_t *p, const mac_label_t *s,
                    const mac_label_t *o);
int  mac_check_write(const mac_policy_t *p, const mac_label_t *s,
                     const mac_label_t *o);
int  mac_label_compare(const mac_label_t *a, const mac_label_t *b);
int  mac_label_dominates(const mac_label_t *a, const mac_label_t *b);
int  mac_label_init_static(mac_label_t *l, const char *name,
                           uint32_t sens, uint32_t cats);

#endif