#ifndef TLS_H
#define TLS_H

#include "stdint.h"
#include "kernel_proc.h"

#define TLS_MAX_KEYS 128

typedef void (*tls_dtor_t)(void *);

int tls_key_create(uint32_t *key, tls_dtor_t destructor);
int tls_key_delete(uint32_t key);
void *tls_get(uint32_t key);
int tls_set(uint32_t key, const void *value);
void tls_cleanup(pcb_t *proc);

#endif
