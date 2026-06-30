#ifndef I18N_H
#define I18N_H

#include "stdint.h"

#define I18N_MAX_LANGS     8
#define I18N_MAX_KEY_LEN   64
#define I18N_MAX_VAL_LEN   256
#define I18N_MAX_MESSAGES  128

#define LANG_ZH_CN  0
#define LANG_EN_US  1

#define LANG_NAME_ZH_CN  "zh_CN"
#define LANG_NAME_EN_US  "en_US"

typedef struct {
    const char *key;
    const char *value;
} i18n_msg_t;

typedef struct {
    char name[16];
    i18n_msg_t *messages;
    int msg_count;
} i18n_lang_t;

void        i18n_init(void);
int         i18n_set_lang(const char *lang_name);
const char *i18n_get_lang(void);
const char *i18n_gettext(const char *key);
const char *i18n_ngettext(const char *key, int n);
int         i18n_add_language(const char *name, i18n_msg_t *msgs, int count);
int         i18n_get_lang_count(void);
const char *i18n_get_lang_name(int index);

#define _(key)  i18n_gettext(key)
#define N_(key) key

#endif
