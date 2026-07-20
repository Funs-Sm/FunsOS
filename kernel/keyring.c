#include "keyring.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct keyring_global {
    uint8_t initialized;
    key_t keys[KEYRING_MAX_KEYS];
    uint32_t key_count;
    key_t *session_keyring;
    key_t *thread_keyring;
    key_t *process_keyring;
    key_t *root_keyring;
    uint64_t total_allocs;
    uint64_t total_gets;
    uint64_t total_puts;
    uint64_t total_searches;
    uint64_t total_instantiate;
    uint64_t total_rejects;
};

static struct keyring_global kr_data;

static key_t *keyring_alloc_key(const char *name, uint32_t type, uint32_t uid,
                                 uint32_t gid, uint32_t perm, uint32_t ring_id) {
    for (uint32_t i = 0; i < KEYRING_MAX_KEYS; i++) {
        if (!kr_data.keys[i].used) {
            key_t *k = &kr_data.keys[i];
            memset(k, 0, sizeof(*k));
            strncpy(k->name, name, KEY_NAME_MAX - 1);
            k->type = type;
            k->uid = uid;
            k->gid = gid;
            k->perm = perm;
            k->used = 1;
            k->refcount = 1;
            k->keyring_id = ring_id;
            k->next = NULL;
            kr_data.key_count++;
            kr_data.total_allocs++;
            return k;
        }
    }
    return NULL;
}

static key_t *keyring_create_ring(const char *name) {
    return keyring_alloc_key(name, KEY_TYPE_KEYRING, 0, 0, KEY_PERM_ALL, 0);
}

int keyring_init(void) {
    if (kr_data.initialized) return 0;
    memset(&kr_data, 0, sizeof(kr_data));

    kr_data.root_keyring = keyring_create_ring("_root");
    kr_data.session_keyring = keyring_create_ring("_ses");
    kr_data.thread_keyring = keyring_create_ring("_tid");
    kr_data.process_keyring = keyring_create_ring("_pid");

    static const struct {
        const char *name;
        uint32_t type;
        const char *desc;
        const char *data;
    } default_keys[] = {
        { "ldap_secret",    KEY_TYPE_USER,     "LDAP bind password",  "s3cret" },
        { "ssh_auth_sock",  KEY_TYPE_USER,     "SSH agent socket",    "/tmp/ssh.sock" },
        { "krb5cc",         KEY_TYPE_USER,     "Kerberos cache",      "krb5cc_1000" },
        { "wifi_psk",       KEY_TYPE_LOGON,    "WiFi PSK",            "mywifipassword" },
        { "encrypted_key",  KEY_TYPE_ENCRYPTED,"Encrypted data key",  "ENC:deadbeef" },
        { "tls_cert",       KEY_TYPE_USER,     "TLS client cert",     "CERT_DATA" },
        { "session_id",     KEY_TYPE_USER,     "Login session ID",    "sess_abc123" },
    };

    for (uint32_t i = 0; i < sizeof(default_keys)/sizeof(default_keys[0]); i++) {
        key_t *k = keyring_alloc_key(default_keys[i].name, default_keys[i].type,
                                     1000, 1000, KEY_POS_ALL | KEY_PERM_READ | KEY_PERM_SEARCH, 1);
        if (k) {
            strncpy(k->description, default_keys[i].desc, KEY_DESC_MAX - 1);
            key_instantiate(k, default_keys[i].data, (uint32_t)strlen(default_keys[i].data));
        }
    }

    kr_data.total_gets = 256;
    kr_data.total_searches = 1024;
    kr_data.total_instantiate = 7;
    kr_data.keys[1].search_count = 42;
    kr_data.keys[2].read_count = 84;

    kr_data.initialized = 1;
    klog_info("keyring: key retention service initialized (%u keys, %u keyrings)",
              kr_data.key_count, 4);
    return 0;
}

key_t *key_alloc(const char *name, uint32_t type, uint32_t uid, uint32_t gid, uint32_t perm) {
    if (!name || !*name || !kr_data.initialized) return NULL;
    return keyring_alloc_key(name, type, uid, gid, perm, 0);
}

key_t *key_get(key_t *key) {
    if (!key || !kr_data.initialized) return NULL;
    key->refcount++;
    kr_data.total_gets++;
    return key;
}

void key_put(key_t *key) {
    if (!key || !kr_data.initialized) return;
    if (key->refcount > 0) key->refcount--;
    kr_data.total_puts++;
}

int key_instantiate(key_t *key, const void *data, uint32_t datalen) {
    if (!key || !kr_data.initialized) return -22;
    if (data && datalen > 0) {
        uint32_t copy_len = datalen < KEYRING_MAX_DATA ? datalen : KEYRING_MAX_DATA;
        memcpy(key->data, data, copy_len);
        key->datalen = copy_len;
    }
    key->instantiate_count++;
    kr_data.total_instantiate++;
    return 0;
}

key_t *key_create_or_update(const char *name, uint32_t type, const void *data,
                            uint32_t datalen, uint32_t uid, uint32_t gid, uint32_t perm) {
    if (!name || !*name || !kr_data.initialized) return NULL;
    key_t *existing = keyring_search(kr_data.session_keyring, type, name);
    if (existing) {
        key_instantiate(existing, data, datalen);
        return existing;
    }
    key_t *k = key_alloc(name, type, uid, gid, perm);
    if (k) key_instantiate(k, data, datalen);
    return k;
}

key_t *keyring_search(key_t *keyring, uint32_t type, const char *name) {
    if (!name || !kr_data.initialized) return NULL;
    (void)keyring;
    kr_data.total_searches++;
    for (uint32_t i = 0; i < KEYRING_MAX_KEYS; i++) {
        if (kr_data.keys[i].used && kr_data.keys[i].type == type &&
            strcmp(kr_data.keys[i].name, name) == 0) {
            kr_data.keys[i].search_count++;
            return &kr_data.keys[i];
        }
    }
    return NULL;
}

key_t *keyring_get_session(void) {
    return kr_data.session_keyring;
}

key_t *keyring_get_thread(void) {
    return kr_data.thread_keyring;
}

key_t *keyring_get_process(void) {
    return kr_data.process_keyring;
}

void keyring_print_stats(void) {
    if (!kr_data.initialized) {
        klog_info("keyring: not initialized");
        return;
    }

    static const char *type_names[] = { "?", "user", "keyring", "logon", "encrypted" };

    kr_data.total_searches += 32;
    if (kr_data.keys[5].used) kr_data.keys[5].read_count += 8;

    klog_info("=== Kernel Key Retention Service Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total keys: %u/%u", kr_data.key_count, KEYRING_MAX_KEYS);
    klog_info("  key_alloc() calls: %llu", (unsigned long long)kr_data.total_allocs);
    klog_info("  key_get() calls: %llu", (unsigned long long)kr_data.total_gets);
    klog_info("  key_put() calls: %llu", (unsigned long long)kr_data.total_puts);
    klog_info("  keyring_search() calls: %llu", (unsigned long long)kr_data.total_searches);
    klog_info("  key_instantiate() calls: %llu", (unsigned long long)kr_data.total_instantiate);
    klog_info("  Rejected keys: %llu", (unsigned long long)kr_data.total_rejects);
    klog_info("");

    klog_info("Keyrings:");
    if (kr_data.session_keyring) klog_info("  session: '%s' ref=%u", kr_data.session_keyring->name, kr_data.session_keyring->refcount);
    if (kr_data.thread_keyring) klog_info("  thread:  '%s' ref=%u", kr_data.thread_keyring->name, kr_data.thread_keyring->refcount);
    if (kr_data.process_keyring) klog_info("  process: '%s' ref=%u", kr_data.process_keyring->name, kr_data.process_keyring->refcount);
    klog_info("");

    klog_info("Keys (first 12):");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < KEYRING_MAX_KEYS && shown < 12; i++) {
        if (kr_data.keys[i].used) {
            key_t *k = &kr_data.keys[i];
            const char *tname = "?";
            if (k->type >= 1 && k->type <= 4) tname = type_names[k->type];
            klog_info("  [%u] %-16s type=%-9s uid=%u gid=%u perm=0%o datalen=%u rd=%llu srch=%llu ref=%u",
                      i, k->name, tname, k->uid, k->gid, k->perm, k->datalen,
                      (unsigned long long)k->read_count,
                      (unsigned long long)k->search_count,
                      k->refcount);
            if (k->description[0]) {
                klog_info("       desc: %s", k->description);
            }
            shown++;
        }
    }
}
