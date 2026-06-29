#include "pkgmgr.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "vfs.h"
#include "http_client.h"
#include "tarfs.h"
#include "sync.h"
#include "spinlock.h"
#include "klog.h"
#include "timer.h"

static pkg_info_t packages[PKG_MAX_PACKAGES];
static uint32_t package_count = 0;
static spinlock_t pkgmgr_lock;

const char *pkgmgr_strerror(int32_t err) {
    switch (err) {
        case PKGMGR_OK:              return "success";
        case PKGMGR_ERR_INVAL:       return "invalid argument";
        case PKGMGR_ERR_NOMEM:       return "out of memory";
        case PKGMGR_ERR_NOENT:       return "package not found";
        case PKGMGR_ERR_EXIST:       return "package already installed";
        case PKGMGR_ERR_IO:          return "I/O error";
        case PKGMGR_ERR_NETWORK:     return "network error";
        case PKGMGR_ERR_BADPKG:      return "bad package format";
        case PKGMGR_ERR_DEPENDENCY:  return "dependency not satisfied";
        case PKGMGR_ERR_FULL:        return "package table full";
        case PKGMGR_ERR_PERM:        return "permission denied";
        default:                     return "unknown error";
    }
}

static void pkgmgr_lock_acquire(uint32_t *flags)
{
    *flags = spinlock_irq_save(&pkgmgr_lock);
}

static void pkgmgr_lock_release(uint32_t flags)
{
    spinlock_irq_restore(&pkgmgr_lock, flags);
}

static int32_t pkgmgr_find_package(const char *name)
{
    uint32_t i;
    if (!name || !*name) return -1;
    for (i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, name) == 0)
            return (int32_t)i;
    }
    return -1;
}

uint32_t pkgmgr_get_count(void)
{
    return package_count;
}

int32_t pkgmgr_get_package(const char *name, pkg_info_t *out_info)
{
    uint32_t flags;
    int32_t idx;

    if (!name || !*name || !out_info) return PKGMGR_ERR_INVAL;

    pkgmgr_lock_acquire(&flags);
    idx = pkgmgr_find_package(name);
    if (idx < 0) {
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_NOENT;
    }
    memcpy(out_info, &packages[idx], sizeof(pkg_info_t));
    pkgmgr_lock_release(flags);
    return PKGMGR_OK;
}

static int32_t pkgmgr_save_db(void)
{
    file_t *file = NULL;
    int32_t ret;

    ret = vfs_open("/var/pkg/db", FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_err("pkgmgr: failed to open db for writing: %d\n", ret);
        return PKGMGR_ERR_IO;
    }

    ret = vfs_write(file, &package_count, sizeof(uint32_t));
    if (ret < (int32_t)sizeof(uint32_t)) {
        klog_err("pkgmgr: failed to write package count to db\n");
        vfs_close(file);
        return PKGMGR_ERR_IO;
    }

    if (package_count > 0) {
        ret = vfs_write(file, packages, sizeof(pkg_info_t) * package_count);
        if (ret < (int32_t)(sizeof(pkg_info_t) * package_count)) {
            klog_err("pkgmgr: failed to write packages to db\n");
            vfs_close(file);
            return PKGMGR_ERR_IO;
        }
    }

    vfs_close(file);
    return PKGMGR_OK;
}

static int32_t pkgmgr_load_db(void)
{
    file_t *file = NULL;
    int32_t ret;

    ret = vfs_open("/var/pkg/db", FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_info("pkgmgr: no existing package db, starting fresh\n");
        return PKGMGR_OK;
    }

    ret = vfs_read(file, &package_count, sizeof(uint32_t));
    if (ret < (int32_t)sizeof(uint32_t)) {
        klog_warn("pkgmgr: failed to read package count from db, resetting\n");
        package_count = 0;
        vfs_close(file);
        return PKGMGR_ERR_IO;
    }

    if (package_count > PKG_MAX_PACKAGES) {
        klog_warn("pkgmgr: db package count (%u) exceeds max (%u), resetting\n",
                  package_count, PKG_MAX_PACKAGES);
        package_count = 0;
        vfs_close(file);
        return PKGMGR_ERR_IO;
    }

    if (package_count > 0) {
        ret = vfs_read(file, packages, sizeof(pkg_info_t) * package_count);
        if (ret < (int32_t)(sizeof(pkg_info_t) * package_count)) {
            klog_warn("pkgmgr: failed to read packages from db, resetting\n");
            package_count = 0;
            vfs_close(file);
            return PKGMGR_ERR_IO;
        }
    }

    vfs_close(file);
    return PKGMGR_OK;
}

void pkgmgr_init(void)
{
    spinlock_init(&pkgmgr_lock);
    memset(packages, 0, sizeof(packages));
    package_count = 0;

    vfs_mkdir("/var", 0);
    vfs_mkdir("/var/pkg", 0);
    vfs_mkdir("/var/pkg/cache", 0);

    pkgmgr_load_db();

    klog_info("pkgmgr: initialized with %u packages\n", package_count);
}

int32_t pkgmgr_install(const char *name)
{
    uint32_t flags;
    int32_t idx;
    http_response_t response;
    tarfs_mount_info_t mount_info;
    superblock_t sb;
    int32_t ret;

    if (!name || !*name) {
        klog_err("pkgmgr: install: package name is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    if (strlen(name) >= sizeof(packages[0].name)) {
        klog_err("pkgmgr: install: package name too long (%zu chars)\n", strlen(name));
        return PKGMGR_ERR_INVAL;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx >= 0 && packages[idx].state == PKG_STATE_INSTALLED) {
        klog_warn("pkgmgr: package '%s' is already installed (version %s)\n",
                  name, packages[idx].version);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_EXIST;
    }

    if (package_count >= PKG_MAX_PACKAGES) {
        klog_err("pkgmgr: package table full (%u packages), cannot install '%s'\n",
                   PKG_MAX_PACKAGES, name);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_FULL;
    }

    pkgmgr_lock_release(flags);

    klog_info("pkgmgr: downloading package '%s'...\n", name);

    memset(&response, 0, sizeof(response));
    ret = http_get(name, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_err("pkgmgr: failed to download package '%s' (http error %d)\n", name, ret);
        return PKGMGR_ERR_NETWORK;
    }

    klog_info("pkgmgr: package '%s' downloaded (%u bytes)\n", name, response.body_len);
    klog_info("pkgmgr: extracting package '%s'...\n", name);

    memset(&mount_info, 0, sizeof(mount_info));
    mount_info.archive_data = response.body;
    mount_info.archive_size = response.body_len;

    memset(&sb, 0, sizeof(sb));
    sb.fs_type = FS_TYPE_TARFS;

    ret = tarfs_mount(&sb, &mount_info);
    if (ret != 0) {
        klog_err("pkgmgr: failed to extract package '%s' (invalid archive?)\n", name);
        http_free_response(&response);
        return PKGMGR_ERR_BADPKG;
    }

    pkgmgr_lock_acquire(&flags);

    if (idx >= 0) {
        packages[idx].state = PKG_STATE_INSTALLED;
        packages[idx].size = response.body_len;
        packages[idx].install_time = (uint32_t)timer_get_ticks();
    } else {
        idx = (int32_t)package_count;
        strncpy(packages[idx].name, name, sizeof(packages[idx].name) - 1);
        packages[idx].name[sizeof(packages[idx].name) - 1] = '\0';
        packages[idx].state = PKG_STATE_INSTALLED;
        strncpy(packages[idx].install_path, "/usr/local",
                sizeof(packages[idx].install_path) - 1);
        packages[idx].install_path[sizeof(packages[idx].install_path) - 1] = '\0';
        packages[idx].size = response.body_len;
        packages[idx].install_time = (uint32_t)timer_get_ticks();
        package_count++;
    }

    ret = pkgmgr_save_db();

    pkgmgr_lock_release(flags);

    http_free_response(&response);

    if (ret != PKGMGR_OK) {
        klog_warn("pkgmgr: warning: failed to save package db\n");
    }

    klog_info("pkgmgr: package '%s' installed successfully (%u bytes)\n",
              name, response.body_len);

    return PKGMGR_OK;
}

int32_t pkgmgr_remove(const char *name)
{
    uint32_t flags;
    int32_t idx;
    char path[512];
    int32_t ret;

    if (!name || !*name) {
        klog_err("pkgmgr: remove: package name is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0) {
        klog_err("pkgmgr: cannot remove '%s': package not found\n", name);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_NOENT;
    }

    if (packages[idx].state != PKG_STATE_INSTALLED) {
        klog_err("pkgmgr: cannot remove '%s': package not installed (state=%u)\n",
                   name, packages[idx].state);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_INVAL;
    }

    klog_info("pkgmgr: removing package '%s'...\n", name);

    memset(path, 0, sizeof(path));
    strncpy(path, packages[idx].install_path, sizeof(path) - 1);
    strncat(path, "/", sizeof(path) - strlen(path) - 1);
    strncat(path, name, sizeof(path) - strlen(path) - 1);

    pkgmgr_lock_release(flags);

    ret = vfs_unlink(path);
    if (ret != 0) {
        klog_warn("pkgmgr: warning: failed to remove files at '%s' (err %d)\n", path, ret);
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx >= 0) {
        if (idx < (int32_t)(package_count - 1)) {
            memcpy(&packages[idx], &packages[package_count - 1],
                   sizeof(pkg_info_t));
        }
        memset(&packages[package_count - 1], 0, sizeof(pkg_info_t));
        package_count--;
    }

    ret = pkgmgr_save_db();

    pkgmgr_lock_release(flags);

    if (ret != PKGMGR_OK) {
        klog_warn("pkgmgr: warning: failed to save package db\n");
    }

    klog_info("pkgmgr: package '%s' removed successfully\n", name);
    return PKGMGR_OK;
}

int32_t pkgmgr_update(const char *name)
{
    uint32_t flags;
    int32_t idx;
    http_response_t response;
    tarfs_mount_info_t mount_info;
    superblock_t sb;
    int32_t ret;
    char old_version[32];
    uint32_t old_size;

    if (!name || !*name) {
        klog_err("pkgmgr: update: package name is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0) {
        klog_err("pkgmgr: cannot update '%s': package not found\n", name);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_NOENT;
    }

    if (packages[idx].state != PKG_STATE_INSTALLED) {
        klog_err("pkgmgr: cannot update '%s': package not installed (state=%u)\n",
                   name, packages[idx].state);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_INVAL;
    }

    packages[idx].state = PKG_STATE_UPDATING;
    strncpy(old_version, packages[idx].version, sizeof(old_version) - 1);
    old_version[sizeof(old_version) - 1] = '\0';
    old_size = packages[idx].size;

    pkgmgr_lock_release(flags);

    klog_info("pkgmgr: updating package '%s' (%s -> ...)...\n", name, old_version);

    memset(&response, 0, sizeof(response));
    ret = http_get(name, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_err("pkgmgr: failed to download update for '%s' (http error %d)\n", name, ret);
        pkgmgr_lock_acquire(&flags);
        idx = pkgmgr_find_package(name);
        if (idx >= 0) packages[idx].state = PKG_STATE_INSTALLED;
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_NETWORK;
    }

    klog_info("pkgmgr: update downloaded for '%s' (%u bytes)\n", name, response.body_len);

    memset(&mount_info, 0, sizeof(mount_info));
    mount_info.archive_data = response.body;
    mount_info.archive_size = response.body_len;

    memset(&sb, 0, sizeof(sb));
    sb.fs_type = FS_TYPE_TARFS;

    ret = tarfs_mount(&sb, &mount_info);
    if (ret != 0) {
        klog_err("pkgmgr: failed to extract update for '%s' (invalid archive?)\n", name);
        http_free_response(&response);
        pkgmgr_lock_acquire(&flags);
        idx = pkgmgr_find_package(name);
        if (idx >= 0) packages[idx].state = PKG_STATE_INSTALLED;
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_BADPKG;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx >= 0) {
        packages[idx].state = PKG_STATE_INSTALLED;
        packages[idx].size = response.body_len;
    }

    ret = pkgmgr_save_db();

    pkgmgr_lock_release(flags);

    http_free_response(&response);

    if (ret != PKGMGR_OK) {
        klog_warn("pkgmgr: warning: failed to save package db\n");
    }

    klog_info("pkgmgr: package '%s' updated (%u bytes -> %u bytes)\n",
              name, old_size, response.body_len);

    return PKGMGR_OK;
}

int32_t pkgmgr_update_all(uint32_t *updated, uint32_t *failed)
{
    uint32_t flags;
    uint32_t i;
    uint32_t count;
    uint32_t ok = 0, fail = 0;
    char names[PKG_MAX_PACKAGES][64];

    pkgmgr_lock_acquire(&flags);
    count = package_count;
    for (i = 0; i < count && i < PKG_MAX_PACKAGES; i++) {
        strncpy(names[i], packages[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
    }
    pkgmgr_lock_release(flags);

    for (i = 0; i < count; i++) {
        int32_t ret = pkgmgr_update(names[i]);
        if (ret == PKGMGR_OK) {
            ok++;
        } else {
            fail++;
            klog_warn("pkgmgr: update_all: failed to update '%s': %s\n",
                      names[i], pkgmgr_strerror(ret));
        }
    }

    if (updated) *updated = ok;
    if (failed) *failed = fail;

    klog_info("pkgmgr: update-all completed: %u updated, %u failed\n", ok, fail);

    return (fail == 0) ? PKGMGR_OK : PKGMGR_ERR_IO;
}

int32_t pkgmgr_list_installed(void)
{
    uint32_t flags;
    uint32_t i;
    uint32_t count = 0;

    pkgmgr_lock_acquire(&flags);

    klog_info("pkgmgr: installed packages (%u total):\n", package_count);
    for (i = 0; i < package_count; i++) {
        if (packages[i].state == PKG_STATE_INSTALLED) {
            klog_info("  %-32s %-16s %8u bytes\n",
                      packages[i].name,
                      packages[i].version[0] ? packages[i].version : "unknown",
                      packages[i].size);
            count++;
        }
    }

    if (count == 0) {
        klog_info("  (no packages installed)\n");
    }

    pkgmgr_lock_release(flags);

    return (int32_t)count;
}

int32_t pkgmgr_search(const char *name)
{
    uint32_t flags;
    uint32_t i;
    int found = 0;

    if (!name || !*name) {
        klog_err("pkgmgr: search: search term is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    pkgmgr_lock_acquire(&flags);

    klog_info("pkgmgr: search results for '%s':\n", name);
    for (i = 0; i < package_count; i++) {
        if (strstr(packages[i].name, name) != NULL) {
            klog_info("  %-32s %-16s %s\n",
                      packages[i].name,
                      packages[i].version[0] ? packages[i].version : "unknown",
                      packages[i].description[0] ? packages[i].description : "no description");
            found++;
        }
    }

    if (found == 0) {
        klog_info("  no matching packages found\n");
    }

    pkgmgr_lock_release(flags);

    return (int32_t)found;
}

int32_t pkgmgr_info(const char *name)
{
    uint32_t flags;
    int32_t idx;
    const char *state_str;

    if (!name || !*name) {
        klog_err("pkgmgr: info: package name is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0) {
        klog_err("pkgmgr: info: package '%s' not found\n", name);
        pkgmgr_lock_release(flags);
        return PKGMGR_ERR_NOENT;
    }

    switch (packages[idx].state) {
        case PKG_STATE_INSTALLED: state_str = "installed"; break;
        case PKG_STATE_AVAILABLE: state_str = "available"; break;
        case PKG_STATE_UPDATING:  state_str = "updating"; break;
        default:                  state_str = "unknown"; break;
    }

    klog_info("Package:     %s\n", packages[idx].name);
    klog_info("Version:     %s\n", packages[idx].version[0] ? packages[idx].version : "unknown");
    klog_info("State:       %s\n", state_str);
    klog_info("Size:        %u bytes\n", packages[idx].size);
    klog_info("Description: %s\n", packages[idx].description[0] ? packages[idx].description : "n/a");
    klog_info("Author:      %s\n", packages[idx].author[0] ? packages[idx].author : "n/a");
    klog_info("License:     %s\n", packages[idx].license[0] ? packages[idx].license : "n/a");
    klog_info("Depends:     %s\n", packages[idx].depends[0] ? packages[idx].depends : "none");
    klog_info("Install path: %s\n", packages[idx].install_path);

    pkgmgr_lock_release(flags);

    return PKGMGR_OK;
}

int32_t pkgmgr_download(const char *url, char *save_path, uint32_t save_path_size)
{
    http_response_t response;
    int32_t ret;
    file_t *file = NULL;
    char path[512];
    const char *filename;

    if (!url || !*url) {
        klog_err("pkgmgr: download: URL is NULL or empty\n");
        return PKGMGR_ERR_INVAL;
    }

    klog_info("pkgmgr: downloading from '%s'...\n", url);

    memset(&response, 0, sizeof(response));
    ret = http_get(url, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_err("pkgmgr: download failed (http error %d)\n", ret);
        return PKGMGR_ERR_NETWORK;
    }

    filename = strrchr(url, '/');
    if (filename && *(filename + 1)) {
        filename++;
    } else {
        filename = "download.bin";
    }

    memset(path, 0, sizeof(path));
    strncpy(path, "/var/pkg/cache/", sizeof(path) - 1);
    strncat(path, filename, sizeof(path) - strlen(path) - 1);

    ret = vfs_open(path, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_err("pkgmgr: failed to create output file '%s' (err %d)\n", path, ret);
        http_free_response(&response);
        return PKGMGR_ERR_IO;
    }

    ret = vfs_write(file, response.body, response.body_len);
    if (ret < (int32_t)response.body_len) {
        klog_err("pkgmgr: failed to write all data (wrote %d of %u bytes)\n",
                   ret, response.body_len);
        vfs_close(file);
        http_free_response(&response);
        return PKGMGR_ERR_IO;
    }

    vfs_close(file);

    if (save_path && save_path_size > 0) {
        strncpy(save_path, path, save_path_size - 1);
        save_path[save_path_size - 1] = '\0';
    }

    klog_info("pkgmgr: downloaded %u bytes to %s\n", response.body_len, path);

    http_free_response(&response);

    return PKGMGR_OK;
}
