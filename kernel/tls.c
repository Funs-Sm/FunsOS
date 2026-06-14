#include "tls.h"
#include "kheap.h"
#include "string.h"
#include "sched.h"

typedef struct {
    uint8_t in_use;
    tls_dtor_t destructor;
} tls_key_desc_t;

static tls_key_desc_t key_table[TLS_MAX_KEYS];
static spinlock_t tls_lock;

void tls_init(void) {
    spinlock_init(&tls_lock);
    for (uint32_t i = 0; i < TLS_MAX_KEYS; i++) {
        key_table[i].in_use = 0;
        key_table[i].destructor = NULL;
    }
}

int tls_key_create(uint32_t *key, tls_dtor_t destructor) {
    if (!key) return -1;

    spinlock_lock(&tls_lock);

    for (uint32_t i = 0; i < TLS_MAX_KEYS; i++) {
        if (!key_table[i].in_use) {
            key_table[i].in_use = 1;
            key_table[i].destructor = destructor;
            *key = i;
            spinlock_unlock(&tls_lock);
            return 0;
        }
    }

    spinlock_unlock(&tls_lock);
    return -1;
}

int tls_key_delete(uint32_t key) {
    if (key >= TLS_MAX_KEYS) return -1;

    spinlock_lock(&tls_lock);

    if (!key_table[key].in_use) {
        spinlock_unlock(&tls_lock);
        return -1;
    }

    key_table[key].in_use = 0;
    key_table[key].destructor = NULL;

    spinlock_unlock(&tls_lock);
    return 0;
}

void *tls_get(uint32_t key) {
    if (key >= TLS_MAX_KEYS) return NULL;

    pcb_t *curr = sched_get_current();
    if (!curr) return NULL;

    return curr->tls_data[key];
}

int tls_set(uint32_t key, const void *value) {
    if (key >= TLS_MAX_KEYS) return -1;

    pcb_t *curr = sched_get_current();
    if (!curr) return -1;

    curr->tls_data[key] = (void *)value;
    return 0;
}

void tls_cleanup(pcb_t *proc) {
    if (!proc) return;

    spinlock_lock(&tls_lock);

    for (uint32_t i = 0; i < TLS_MAX_KEYS; i++) {
        if (key_table[i].in_use && key_table[i].destructor && proc->tls_data[i]) {
            tls_dtor_t dtor = key_table[i].destructor;
            void *data = proc->tls_data[i];
            proc->tls_data[i] = NULL;
            spinlock_unlock(&tls_lock);
            dtor(data);
            spinlock_lock(&tls_lock);
        }
    }

    /* Clear all TLS data */
    for (uint32_t i = 0; i < TLS_MAX_KEYS; i++) {
        proc->tls_data[i] = NULL;
    }

    spinlock_unlock(&tls_lock);
}
