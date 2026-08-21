/* mac_label.c - Static Mandatory Access Control labels.
 *
 * Provides a tiny policy engine that resolves read / write decisions
 * based on sensitivity levels and category coverage. The verification
 * does not require compile-time configuration; the policy is built
 * programmatically from a small struct.
 */

#include "mac_label.h"
#include "string.h"

int mac_label_init_static(mac_label_t *l, const char *name,
                           uint32_t sens, uint32_t cats)
{
    if (!l || !name) return -1;
    int i = 0;
    while (name[i] && i < MAC_LABEL_LEN) {
        l->name[i] = name[i];
        i++;
    }
    l->name[i] = 0;
    l->sensitivity = sens;
    l->categories = cats;
    l->reserved[0] = l->reserved[1] = l->reserved[2] = 0;
    return 0;
}

int mac_init(mac_policy_t *p) {
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    return 0;
}

int mac_add_label(mac_policy_t *p, const char *name, uint32_t sens,
                   uint32_t cats)
{
    if (!p || !name) return -1;
    /* Find the next free slot in classes - labels live as members
     * of the first available class. */
    if (p->n_classes == 0) {
        mac_class_t *c = &p->classes[0];
        mac_label_init_static(&c->subjects[0], name, sens, cats);
        c->n_subjects = 1;
        c->n_objects = 1;
        memcpy(&c->objects[0], &c->subjects[0], sizeof(mac_label_t));
        p->n_classes = 1;
        return 0;
    }
    /* Add to the current class until full. */
    mac_class_t *c = &p->classes[p->n_classes - 1];
    if (c->n_subjects < MAC_OBJ_MAX_SUBJECTS) {
        mac_label_init_static(&c->subjects[c->n_subjects], name, sens, cats);
        c->n_subjects++;
        if (c->n_objects < MAC_OBJ_MAX_SUBJECTS) {
            mac_label_init_static(&c->objects[c->n_objects], name, sens, cats);
            c->n_objects++;
        }
        return 0;
    }
    if (p->n_classes >= 16) return -1;
    p->n_classes++;
    c = &p->classes[p->n_classes - 1];
    mac_label_init_static(&c->subjects[0], name, sens, cats);
    c->n_subjects = 1;
    c->n_objects = 1;
    memcpy(&c->objects[0], &c->subjects[0], sizeof(mac_label_t));
    return 0;
}

int mac_label_compare(const mac_label_t *a, const mac_label_t *b) {
    if (!a || !b) return -1;
    if (a->sensitivity == b->sensitivity
        && a->categories == b->categories
        && strcmp(a->name, b->name) == 0) {
        return 0;
    }
    return 1;
}

int mac_label_dominates(const mac_label_t *a, const mac_label_t *b) {
    if (!a || !b) return 0;
    /* a "dominates" b iff a.sens >= b.sens AND a.cats superset b.cats */
    if (a->sensitivity < b->sensitivity) return 0;
    if ((a->categories & b->categories) != b->categories) return 0;
    /* Strict comparison: actual dominance requires at least one strict. */
    if (a->sensitivity == b->sensitivity
        && a->categories == b->categories) return 1;  /* equal */
    return 1;
}

int mac_check_read(const mac_policy_t *p, const mac_label_t *s,
                    const mac_label_t *o)
{
    if (!p || !s || !o) return 0;
    /* Read allowed if subject dominates object (no leakage upward). */
    if (mac_label_dominates(s, o)) { ((mac_policy_t *)p)->granted++; return 1; }
    ((mac_policy_t *)p)->denied++;
    return 0;
}

int mac_check_write(const mac_policy_t *p, const mac_label_t *s,
                     const mac_label_t *o)
{
    if (!p || !s || !o) return 0;
    /* Write allowed only if labels are equal - no write-up. */
    if (mac_label_compare(s, o) == 0) {
        ((mac_policy_t *)p)->granted++; return 1;
    }
    ((mac_policy_t *)p)->denied++;
    return 0;
}

int mac_check_access(const mac_policy_t *p, const mac_label_t *s,
                      const mac_label_t *o)
{
    if (!p || !s || !o) return 0;
    /* Access allowed if subject dominates object (no write-up, no read-up). */
    if (mac_label_dominates(s, o)) {
        ((mac_policy_t *)p)->granted++; return 1;
    }
    ((mac_policy_t *)p)->denied++;
    return 0;
}