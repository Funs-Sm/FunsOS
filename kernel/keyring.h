#ifndef KEYRING_H
#define KEYRING_H

#include "stdint.h"

#define KEY_NAME_MAX         64
#define KEY_DESC_MAX        128
#define KEYRING_MAX_KEYS    128
#define KEYRING_MAX_DATA    256
#define KEYRING_MAX_RINGS    16

#define KEY_TYPE_USER       1
#define KEY_TYPE_KEYRING    2
#define KEY_TYPE_LOGON      3
#define KEY_TYPE_ENCRYPTED  4

#define KEY_PERM_VIEW    0x01
#define KEY_PERM_READ    0x02
#define KEY_PERM_WRITE   0x04
#define KEY_PERM_SEARCH  0x08
#define KEY_PERM_LINK    0x10
#define KEY_PERM_ALL     0x1F

#define KEY_POS_VIEW     0x01000000
#define KEY_POS_READ     0x02000000
#define KEY_POS_WRITE    0x04000000
#define KEY_POS_SEARCH   0x08000000
#define KEY_POS_LINK     0x10000000
#define KEY_POS_ALL      0x3F000000

struct key;
typedef struct key key_t;

struct key {
    char name[KEY_NAME_MAX];
    char description[KEY_DESC_MAX];
    uint32_t type;
    uint32_t perm;
    uint32_t uid;
    uint32_t gid;
    uint32_t used;
    uint32_t datalen;
    uint8_t data[KEYRING_MAX_DATA];
    uint32_t refcount;
    uint32_t keyring_id;
    key_t *next;
    uint64_t read_count;
    uint64_t search_count;
    uint64_t instantiate_count;
};

int keyring_init(void);
key_t *key_alloc(const char *name, uint32_t type, uint32_t uid, uint32_t gid, uint32_t perm);
key_t *key_get(key_t *key);
void key_put(key_t *key);
int key_instantiate(key_t *key, const void *data, uint32_t datalen);
key_t *key_create_or_update(const char *name, uint32_t type, const void *data,
                            uint32_t datalen, uint32_t uid, uint32_t gid, uint32_t perm);
key_t *keyring_search(key_t *keyring, uint32_t type, const char *name);
key_t *keyring_get_session(void);
key_t *keyring_get_thread(void);
key_t *keyring_get_process(void);
void keyring_print_stats(void);

#endif
