#include "env.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "spinlock.h"

static sysenv_table_t *sys_env = NULL;
static spinlock_t env_lock;

static int sysenv_find_entry(sysenv_table_t *table, const char *name) {
    if (!table || !name) return -1;
    for (int i = 0; i < SYSENV_MAX_VARS; i++) {
        if (table->entries[i].used &&
            strncmp(table->entries[i].name, name, SYSENV_MAX_NAME) == 0) {
            return i;
        }
    }
    return -1;
}

static int sysenv_find_free(sysenv_table_t *table) {
    if (!table) return -1;
    for (int i = 0; i < SYSENV_MAX_VARS; i++) {
        if (!table->entries[i].used) {
            return i;
        }
    }
    return -1;
}

void sysenv_init(void) {
    spinlock_init(&env_lock);
    sys_env = (sysenv_table_t *)kmalloc(sizeof(sysenv_table_t));
    if (!sys_env) return;
    memset(sys_env, 0, sizeof(sysenv_table_t));

    sysenv_set_sys("PATH", "/bin:/sbin:/usr/bin:/usr/local/bin");
    sysenv_set_sys("HOME", "/root");
    sysenv_set_sys("USER", "root");
    sysenv_set_sys("SHELL", "/bin/sh");
    sysenv_set_sys("TERM", "linux");
    sysenv_set_sys("PWD", "/");
    sysenv_set_sys("LANG", "C");
}

int sysenv_set(const char *name, const char *value, int overwrite) {
    if (!name || !value || !*name) return -1;
    if (!sys_env) return -1;

    spinlock_lock(&env_lock);

    int idx = sysenv_find_entry(sys_env, name);
    if (idx >= 0) {
        if (!overwrite) {
            spinlock_unlock(&env_lock);
            return 0;
        }
        strncpy(sys_env->entries[idx].value, value, SYSENV_MAX_VALUE - 1);
        sys_env->entries[idx].value[SYSENV_MAX_VALUE - 1] = '\0';
        spinlock_unlock(&env_lock);
        return 0;
    }

    idx = sysenv_find_free(sys_env);
    if (idx < 0) {
        spinlock_unlock(&env_lock);
        return -1;
    }

    strncpy(sys_env->entries[idx].name, name, SYSENV_MAX_NAME - 1);
    sys_env->entries[idx].name[SYSENV_MAX_NAME - 1] = '\0';
    strncpy(sys_env->entries[idx].value, value, SYSENV_MAX_VALUE - 1);
    sys_env->entries[idx].value[SYSENV_MAX_VALUE - 1] = '\0';
    sys_env->entries[idx].used = 1;
    sys_env->count++;

    spinlock_unlock(&env_lock);
    return 0;
}

int sysenv_set_sys(const char *name, const char *value) {
    return sysenv_set(name, value, 1);
}

int sysenv_unset(const char *name) {
    if (!name || !sys_env) return -1;

    spinlock_lock(&env_lock);

    int idx = sysenv_find_entry(sys_env, name);
    if (idx < 0) {
        spinlock_unlock(&env_lock);
        return -1;
    }

    memset(&sys_env->entries[idx], 0, sizeof(sysenv_entry_t));
    sys_env->count--;

    spinlock_unlock(&env_lock);
    return 0;
}

const char *sysenv_get(const char *name) {
    if (!name || !sys_env) return NULL;

    spinlock_lock(&env_lock);

    int idx = sysenv_find_entry(sys_env, name);
    if (idx < 0) {
        spinlock_unlock(&env_lock);
        return NULL;
    }

    const char *val = sys_env->entries[idx].value;
    spinlock_unlock(&env_lock);
    return val;
}

int sysenv_get_all(char *buf, uint32_t buf_size, uint32_t *out_count) {
    if (!buf || buf_size == 0 || !sys_env) return -1;

    spinlock_lock(&env_lock);

    uint32_t pos = 0;
    uint32_t count = 0;

    for (int i = 0; i < SYSENV_MAX_VARS; i++) {
        if (!sys_env->entries[i].used) continue;

        uint32_t nlen = 0;
        while (sys_env->entries[i].name[nlen] && nlen < SYSENV_MAX_NAME) nlen++;
        uint32_t vlen = 0;
        while (sys_env->entries[i].value[vlen] && vlen < SYSENV_MAX_VALUE) vlen++;

        if (pos + nlen + 1 + vlen + 1 > buf_size) break;

        memcpy(buf + pos, sys_env->entries[i].name, nlen);
        pos += nlen;
        buf[pos++] = '=';
        memcpy(buf + pos, sys_env->entries[i].value, vlen);
        pos += vlen;
        buf[pos++] = '\n';
        count++;
    }

    if (pos < buf_size) buf[pos] = '\0';

    spinlock_unlock(&env_lock);

    if (out_count) *out_count = count;
    return (int)pos;
}

uint32_t sysenv_count(void) {
    if (!sys_env) return 0;
    return sys_env->count;
}

void sysenv_reset(void) {
    if (!sys_env) return;
    spinlock_lock(&env_lock);
    memset(sys_env, 0, sizeof(sysenv_table_t));
    spinlock_unlock(&env_lock);
}
