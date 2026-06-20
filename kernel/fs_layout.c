#include "fs_layout.h"
#include "vfs.h"
#include "klog.h"
#include "string.h"

/*
 * fs_layout.c - FUNSOS 文件系统目录布局初始化
 *
 * 在 VFS 初始化后、shell 启动前调用，自动创建标准 Unix 风格目录结构。
 * 如果目录已存在，vfs_mkdir 返回错误，忽略即可。
 */

/* 辅助宏：尝试创建目录，忽略已存在的错误 */
#define TRY_MKDIR(path) do { \
    if (vfs_mkdir((path), FILE_MODE_DIR | 0755) == 0) { \
        klog_info("fs_layout: created %s", (path)); \
    } \
} while (0)

/* Self-test for the VFS/ramfs improvements.  Exercises mkdir, creat,
 * open(O_CREATE), read/write, symlink/readlink and readdir. */
static void fs_run_self_tests(void) {
    const char *test_dir = "/fs_test";
    const char *test_file = "/fs_test/hello.txt";
    const char *test_link = "/fs_test/hello_link";
    const char *test_subdir = "/fs_test/subdir";
    char buf[128];
    file_t *f = NULL;

    klog_info("fs_self_test: starting VFS self-tests");

    if (vfs_mkdir(test_dir, FILE_MODE_DIR | 0755) != 0) {
        klog_warn("fs_self_test: mkdir %s failed", test_dir);
        return;
    }
    klog_info("fs_self_test: created %s", test_dir);

    if (vfs_open(test_file, FILE_MODE_CREATE | FILE_MODE_WRITE, &f) != 0) {
        klog_warn("fs_self_test: create %s failed", test_file);
        return;
    }
    const char *msg = "Hello from VFS self-test!";
    int32_t wlen = vfs_write(f, msg, strlen(msg));
    vfs_close(f);
    if (wlen != (int32_t)strlen(msg)) {
        klog_warn("fs_self_test: write returned %d", wlen);
        return;
    }
    klog_info("fs_self_test: wrote %d bytes to %s", wlen, test_file);

    if (vfs_open(test_file, FILE_MODE_READ, &f) != 0) {
        klog_warn("fs_self_test: open %s for read failed", test_file);
        return;
    }
    int32_t rlen = vfs_read(f, buf, sizeof(buf) - 1);
    vfs_close(f);
    if (rlen != wlen) {
        klog_warn("fs_self_test: read returned %d", rlen);
        return;
    }
    buf[rlen] = '\0';
    klog_info("fs_self_test: read back: '%s'", buf);

    if (vfs_symlink(test_file, test_link) != 0) {
        klog_warn("fs_self_test: symlink %s failed", test_link);
        return;
    }
    klog_info("fs_self_test: created symlink %s -> %s", test_link, test_file);

    char link_target[128];
    if (vfs_readlink(test_link, link_target, sizeof(link_target)) < 0) {
        klog_warn("fs_self_test: readlink %s failed", test_link);
        return;
    }
    klog_info("fs_self_test: readlink returned: '%s'", link_target);

    if (vfs_open(test_link, FILE_MODE_READ, &f) != 0) {
        klog_warn("fs_self_test: open symlink %s failed", test_link);
        return;
    }
    rlen = vfs_read(f, buf, sizeof(buf) - 1);
    vfs_close(f);
    if (rlen != wlen) {
        klog_warn("fs_self_test: read via symlink returned %d", rlen);
        return;
    }
    buf[rlen] = '\0';
    klog_info("fs_self_test: read via symlink: '%s'", buf);

    if (vfs_mkdir(test_subdir, FILE_MODE_DIR | 0755) != 0) {
        klog_warn("fs_self_test: mkdir %s failed", test_subdir);
        return;
    }
    klog_info("fs_self_test: created %s", test_subdir);

    file_t *dir = NULL;
    if (vfs_opendir(test_dir, &dir) != 0) {
        klog_warn("fs_self_test: opendir %s failed", test_dir);
        return;
    }
    vfs_dirent_t entry;
    klog_info("fs_self_test: listing %s:", test_dir);
    while (vfs_readdir(dir, &entry) == 1) {
        const char *typestr = (entry.type == DT_DIR) ? "DIR" :
                              (entry.type == DT_LNK) ? "LNK" : "REG";
        klog_info("  [%s] %s", typestr, entry.name);
    }
    vfs_closedir(dir);

    klog_info("fs_self_test: all tests passed");
}

void fs_build_layout(void) {
    klog_info("fs_build_layout: building standard Unix directory structure...");

    /* ===== 用户家目录 ===== */
    TRY_MKDIR("/home");
    TRY_MKDIR("/home/root");
    TRY_MKDIR("/home/admin");
    TRY_MKDIR("/home/guest");

    /* ===== 临时文件目录 ===== */
    TRY_MKDIR("/tmp");

    /* ===== 可变数据目录 ===== */
    TRY_MKDIR("/var");
    TRY_MKDIR("/var/log");
    TRY_MKDIR("/var/run");

    /* ===== 配置文件目录 ===== */
    TRY_MKDIR("/etc");

    /* ===== 用户程序目录 ===== */
    TRY_MKDIR("/usr");
    TRY_MKDIR("/usr/bin");
    TRY_MKDIR("/usr/lib");

    /* ===== 系统管理程序 ===== */
    TRY_MKDIR("/sbin");

    /* ===== 可选软件包 ===== */
    TRY_MKDIR("/opt");

    /* ===== 挂载点 ===== */
    TRY_MKDIR("/mnt");
    TRY_MKDIR("/mnt/disk");
    TRY_MKDIR("/mnt/usb");

    /* ===== 虚拟文件系统挂载点（如果还没挂载则创建）===== */
    TRY_MKDIR("/proc");
    TRY_MKDIR("/sys");

    klog_info("fs_build_layout: directory structure ready");

    fs_run_self_tests();
}
