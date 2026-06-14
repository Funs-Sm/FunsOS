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

static pkg_info_t packages[PKG_MAX_PACKAGES];
static uint32_t package_count = 0;
static spinlock_t pkgmgr_lock;

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
    for (i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, name) == 0)
            return (int32_t)i;
    }
    return -1;
}

static void pkgmgr_save_db(void)
{
    file_t *file = NULL;
    int32_t ret;

    ret = vfs_open("/var/pkg/db", FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_info("pkgmgr: failed to open db for writing\n");
        return;
    }

    ret = vfs_write(file, &package_count, sizeof(uint32_t));
    if (ret < 0) {
        klog_info("pkgmgr: failed to write package count to db\n");
        vfs_close(file);
        return;
    }

    ret = vfs_write(file, packages, sizeof(pkg_info_t) * package_count);
    if (ret < 0) {
        klog_info("pkgmgr: failed to write packages to db\n");
    }

    vfs_close(file);
}

static void pkgmgr_load_db(void)
{
    file_t *file = NULL;
    int32_t ret;

    ret = vfs_open("/var/pkg/db", FILE_MODE_READ | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_info("pkgmgr: no existing package db, starting fresh\n");
        return;
    }

    ret = vfs_read(file, &package_count, sizeof(uint32_t));
    if (ret < (int32_t)sizeof(uint32_t)) {
        klog_info("pkgmgr: failed to read package count from db\n");
        package_count = 0;
        vfs_close(file);
        return;
    }

    if (package_count > PKG_MAX_PACKAGES) {
        klog_info("pkgmgr: db package count exceeds max, resetting\n");
        package_count = 0;
        vfs_close(file);
        return;
    }

    ret = vfs_read(file, packages, sizeof(pkg_info_t) * package_count);
    if (ret < (int32_t)(sizeof(pkg_info_t) * package_count)) {
        klog_info("pkgmgr: failed to read packages from db\n");
        package_count = 0;
    }

    vfs_close(file);
}

void pkgmgr_init(void)
{
    spinlock_init(&pkgmgr_lock);
    memset(packages, 0, sizeof(packages));
    package_count = 0;

    vfs_mkdir("/var", 0);
    vfs_mkdir("/var/pkg", 0);

    pkgmgr_load_db();

    klog_info("pkgmgr: initialized with %u packages\n", package_count);
}

void pkgmgr_install(const char *name)
{
    uint32_t flags;
    int32_t idx;
    http_response_t response;
    tarfs_mount_info_t mount_info;
    superblock_t sb;
    int32_t ret;

    if (name == NULL) {
        klog_info("pkgmgr: install called with NULL name\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx >= 0 && packages[idx].state == PKG_STATE_INSTALLED) {
        klog_info("pkgmgr: package '%s' is already installed\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    if (package_count >= PKG_MAX_PACKAGES) {
        klog_info("pkgmgr: package table full, cannot install '%s'\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    klog_info("pkgmgr: downloading package '%s'\n", name);

    memset(&response, 0, sizeof(response));
    ret = http_get(name, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_info("pkgmgr: failed to download package '%s'\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    klog_info("pkgmgr: extracting package '%s' (%u bytes)\n", name, response.body_len);

    memset(&mount_info, 0, sizeof(mount_info));
    mount_info.archive_data = response.body;
    mount_info.archive_size = response.body_len;

    memset(&sb, 0, sizeof(sb));
    sb.fs_type = FS_TYPE_TARFS;

    ret = tarfs_mount(&sb, &mount_info);
    if (ret != 0) {
        klog_info("pkgmgr: failed to extract package '%s'\n", name);
        http_free_response(&response);
        pkgmgr_lock_release(flags);
        return;
    }

    if (idx >= 0) {
        packages[idx].state = PKG_STATE_INSTALLED;
        packages[idx].size = response.body_len;
    } else {
        idx = (int32_t)package_count;
        strncpy(packages[idx].name, name, sizeof(packages[idx].name) - 1);
        packages[idx].name[sizeof(packages[idx].name) - 1] = '\0';
        packages[idx].state = PKG_STATE_INSTALLED;
        strncpy(packages[idx].install_path, "/usr/local",
                sizeof(packages[idx].install_path) - 1);
        packages[idx].install_path[sizeof(packages[idx].install_path) - 1] = '\0';
        packages[idx].size = response.body_len;
        package_count++;
    }

    http_free_response(&response);

    pkgmgr_save_db();

    klog_info("pkgmgr: package '%s' installed successfully\n", name);

    pkgmgr_lock_release(flags);
}

void pkgmgr_remove(const char *name)
{
    uint32_t flags;
    int32_t idx;
    char path[256];

    if (name == NULL) {
        klog_info("pkgmgr: remove called with NULL name\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0 || packages[idx].state != PKG_STATE_INSTALLED) {
        klog_info("pkgmgr: package '%s' is not installed\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    klog_info("pkgmgr: removing package '%s'\n", name);

    memset(path, 0, sizeof(path));
    strncpy(path, packages[idx].install_path, sizeof(path) - 1);
    strncat(path, "/", sizeof(path) - strlen(path) - 1);
    strncat(path, name, sizeof(path) - strlen(path) - 1);
    vfs_unlink(path);

    if (idx < (int32_t)(package_count - 1)) {
        memcpy(&packages[idx], &packages[package_count - 1],
               sizeof(pkg_info_t));
    }
    memset(&packages[package_count - 1], 0, sizeof(pkg_info_t));
    package_count--;

    pkgmgr_save_db();

    klog_info("pkgmgr: package '%s' removed successfully\n", name);

    pkgmgr_lock_release(flags);
}

void pkgmgr_update(const char *name)
{
    uint32_t flags;
    int32_t idx;
    http_response_t response;
    tarfs_mount_info_t mount_info;
    superblock_t sb;
    int32_t ret;
    char old_version[32];

    if (name == NULL) {
        klog_info("pkgmgr: update called with NULL name\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0 || packages[idx].state != PKG_STATE_INSTALLED) {
        klog_info("pkgmgr: package '%s' is not installed\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    packages[idx].state = PKG_STATE_UPDATING;

    klog_info("pkgmgr: updating package '%s'\n", name);

    strncpy(old_version, packages[idx].version, sizeof(old_version) - 1);
    old_version[sizeof(old_version) - 1] = '\0';

    memset(&response, 0, sizeof(response));
    ret = http_get(name, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_info("pkgmgr: failed to download update for '%s'\n", name);
        packages[idx].state = PKG_STATE_INSTALLED;
        pkgmgr_lock_release(flags);
        return;
    }

    memset(&mount_info, 0, sizeof(mount_info));
    mount_info.archive_data = response.body;
    mount_info.archive_size = response.body_len;

    memset(&sb, 0, sizeof(sb));
    sb.fs_type = FS_TYPE_TARFS;

    ret = tarfs_mount(&sb, &mount_info);
    if (ret != 0) {
        klog_info("pkgmgr: failed to extract update for '%s'\n", name);
        http_free_response(&response);
        packages[idx].state = PKG_STATE_INSTALLED;
        pkgmgr_lock_release(flags);
        return;
    }

    packages[idx].state = PKG_STATE_INSTALLED;
    packages[idx].size = response.body_len;

    http_free_response(&response);

    pkgmgr_save_db();

    klog_info("pkgmgr: package '%s' updated from %s\n", name, old_version);

    pkgmgr_lock_release(flags);
}

void pkgmgr_update_all(void)
{
    uint32_t flags;
    uint32_t i;
    uint32_t count;

    pkgmgr_lock_acquire(&flags);

    count = package_count;

    pkgmgr_lock_release(flags);

    for (i = 0; i < count; i++) {
        pkgmgr_lock_acquire(&flags);

        if (packages[i].state == PKG_STATE_INSTALLED) {
            char name[64];
            strncpy(name, packages[i].name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            pkgmgr_lock_release(flags);
            pkgmgr_update(name);
        } else {
            pkgmgr_lock_release(flags);
        }
    }

    klog_info("pkgmgr: update all completed\n");
}

void pkgmgr_list_installed(void)
{
    uint32_t flags;
    uint32_t i;

    pkgmgr_lock_acquire(&flags);

    klog_info("pkgmgr: installed packages:\n");
    for (i = 0; i < package_count; i++) {
        if (packages[i].state == PKG_STATE_INSTALLED) {
            klog_info("  %-32s %s\n", packages[i].name, packages[i].version);
        }
    }

    pkgmgr_lock_release(flags);
}

void pkgmgr_search(const char *name)
{
    uint32_t flags;
    uint32_t i;
    int found = 0;

    if (name == NULL) {
        klog_info("pkgmgr: search called with NULL name\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    klog_info("pkgmgr: search results for '%s':\n", name);
    for (i = 0; i < package_count; i++) {
        if (strstr(packages[i].name, name) != NULL) {
            klog_info("  %-32s %-16s %s\n",
                      packages[i].name, packages[i].version,
                      packages[i].description);
            found = 1;
        }
    }

    if (!found) {
        klog_info("  no packages found\n");
    }

    pkgmgr_lock_release(flags);
}

void pkgmgr_info(const char *name)
{
    uint32_t flags;
    int32_t idx;

    if (name == NULL) {
        klog_info("pkgmgr: info called with NULL name\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    idx = pkgmgr_find_package(name);
    if (idx < 0) {
        klog_info("pkgmgr: package '%s' not found\n", name);
        pkgmgr_lock_release(flags);
        return;
    }

    klog_info("Package: %s\n", packages[idx].name);
    klog_info("Version: %s\n", packages[idx].version);
    klog_info("Description: %s\n", packages[idx].description);
    klog_info("State: %u\n", packages[idx].state);
    klog_info("Size: %u\n", packages[idx].size);
    klog_info("Depends: %s\n", packages[idx].depends);
    klog_info("Install path: %s\n", packages[idx].install_path);

    pkgmgr_lock_release(flags);
}

void pkgmgr_download(const char *url)
{
    uint32_t flags;
    http_response_t response;
    int32_t ret;
    file_t *file = NULL;
    char path[256];

    if (url == NULL) {
        klog_info("pkgmgr: download called with NULL url\n");
        return;
    }

    pkgmgr_lock_acquire(&flags);

    klog_info("pkgmgr: downloading from '%s'\n", url);

    memset(&response, 0, sizeof(response));
    ret = http_get(url, &response);
    if (ret != 0 || response.body == NULL || response.body_len == 0) {
        klog_info("pkgmgr: failed to download from '%s'\n", url);
        pkgmgr_lock_release(flags);
        return;
    }

    memset(path, 0, sizeof(path));
    strncpy(path, "/var/pkg/", sizeof(path) - 1);
    strncat(path, url, sizeof(path) - strlen(path) - 1);

    ret = vfs_open(path, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (ret != 0 || file == NULL) {
        klog_info("pkgmgr: failed to create file for download\n");
        http_free_response(&response);
        pkgmgr_lock_release(flags);
        return;
    }

    ret = vfs_write(file, response.body, response.body_len);
    if (ret < 0) {
        klog_info("pkgmgr: failed to write downloaded data\n");
    }

    vfs_close(file);

    klog_info("pkgmgr: downloaded %u bytes to %s\n", response.body_len, path);

    http_free_response(&response);

    pkgmgr_lock_release(flags);
}
