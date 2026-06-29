#ifndef PKGMGR_H
#define PKGMGR_H

#include "stdint.h"

#define PKG_STATE_INSTALLED  1
#define PKG_STATE_AVAILABLE  2
#define PKG_STATE_UPDATING   3

#define PKG_MAX_PACKAGES 256

#define PKGMGR_OK              0
#define PKGMGR_ERR_INVAL      -1
#define PKGMGR_ERR_NOMEM      -2
#define PKGMGR_ERR_NOENT      -3
#define PKGMGR_ERR_EXIST      -4
#define PKGMGR_ERR_IO         -5
#define PKGMGR_ERR_NETWORK    -6
#define PKGMGR_ERR_BADPKG     -7
#define PKGMGR_ERR_DEPENDENCY -8
#define PKGMGR_ERR_FULL       -9
#define PKGMGR_ERR_PERM       -10

typedef struct {
    char     name[64];
    char     version[32];
    char     description[128];
    uint32_t state;
    uint32_t size;
    char     depends[256];
    char     install_path[128];
    char     author[64];
    char     license[32];
    uint32_t install_time;
} pkg_info_t;

void pkgmgr_init(void);
int32_t pkgmgr_install(const char *name);
int32_t pkgmgr_remove(const char *name);
int32_t pkgmgr_update(const char *name);
int32_t pkgmgr_update_all(uint32_t *updated, uint32_t *failed);
int32_t pkgmgr_list_installed(void);
int32_t pkgmgr_search(const char *name);
int32_t pkgmgr_info(const char *name);
int32_t pkgmgr_download(const char *url, char *save_path, uint32_t save_path_size);

int32_t pkgmgr_get_package(const char *name, pkg_info_t *out_info);
uint32_t pkgmgr_get_count(void);
const char *pkgmgr_strerror(int32_t err);

#endif
