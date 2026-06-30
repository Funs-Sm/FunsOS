#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include <stdint.h>

#define APP_MAX_COUNT 32

typedef enum {
    APP_TYPE_SYSTEM = 0,
    APP_TYPE_UTILITY = 1,
    APP_TYPE_GAME = 2,
    APP_TYPE_DEVELOPMENT = 3,
    APP_TYPE_OTHER = 4
} app_type_t;

typedef struct {
    const char *name;
    const char *description;
    app_type_t type;
    void (*launch)(void);
    uint8_t builtin;
    uint8_t enabled;
} app_entry_t;

void app_registry_init(void);
int app_register(const char *name, const char *desc, app_type_t type, void (*launch)(void), int builtin);
int app_unregister(const char *name);
app_entry_t *app_find(const char *name);
app_entry_t *app_get_by_index(int index);
int app_get_count(void);
int app_launch(const char *name);
void app_list_all(void);

#endif
