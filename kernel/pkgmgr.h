#ifndef PKGMGR_H
#define PKGMGR_H

#include "stdint.h"

#define PKG_STATE_INSTALLED  1
#define PKG_STATE_AVAILABLE  2
#define PKG_STATE_UPDATING   3

#define PKG_MAX_PACKAGES 256

typedef struct {
    char     name[64];
    char     version[32];
    char     description[128];
    uint32_t state;
    uint32_t size;
    char     depends[256];
    char     install_path[128];
} pkg_info_t;

void pkgmgr_init(void);
void pkgmgr_install(const char *name);
void pkgmgr_remove(const char *name);
void pkgmgr_update(const char *name);
void pkgmgr_update_all(void);
void pkgmgr_list_installed(void);
void pkgmgr_search(const char *name);
void pkgmgr_info(const char *name);
void pkgmgr_download(const char *url);

#endif
