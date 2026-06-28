#include "fs_layout.h"
#include "vfs.h"
#include "klog.h"
#include "string.h"
#include "version.h"

#define TRY_MKDIR(path) do { \
    if (vfs_mkdir((path), FILE_MODE_DIR | 0755) == 0) { \
        klog_info("fs_layout: created %s", (path)); \
    } \
} while (0)

static void TRY_WRITE(const char *path, const char *data, uint32_t mode) {
    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_CREATE | FILE_MODE_WRITE, &f) != 0 || !f) {
        return;
    }
    uint32_t len = 0;
    while (data[len]) len++;
    vfs_write(f, data, (int32_t)len);
    vfs_close(f);
    (void)mode;
    klog_info("fs_layout: wrote %s (%u bytes)", path, len);
}

/* Self-test for the VFS/ramfs.  Kept minimal and non-fatal: any failure
 * is logged as a warning but does not block boot.  Real functionality
 * is verified interactively from the shell. */
static void fs_run_self_tests(void) {
    const char *test_file = "/tmp/fstest.txt";
    char buf[64];
    file_t *f = NULL;

    klog_info("VFS self-test: quick smoke test");

    if (vfs_open(test_file, FILE_MODE_CREATE | FILE_MODE_WRITE, &f) != 0) {
        klog_warn("VFS self-test: create %s failed (non-fatal)", test_file);
        return;
    }
    const char *msg = "FunOS VFS OK";
    int32_t wlen = vfs_write(f, msg, (int32_t)strlen(msg));
    vfs_close(f);
    if (wlen != (int32_t)strlen(msg)) {
        klog_warn("VFS self-test: write len mismatch (%d)", wlen);
        return;
    }

    if (vfs_open(test_file, FILE_MODE_READ, &f) != 0) {
        klog_warn("VFS self-test: reopen for read failed");
        return;
    }
    int32_t rlen = vfs_read(f, buf, sizeof(buf) - 1);
    vfs_close(f);
    buf[rlen > 0 ? rlen : 0] = '\0';
    vfs_unlink(test_file);

    if (rlen == wlen && memcmp(buf, msg, wlen) == 0) {
        klog_info("VFS self-test: OK");
    } else {
        klog_warn("VFS self-test: read-back mismatch (got %d bytes)", rlen);
    }
}

void fs_build_layout(void) {
    klog_info("fs_build_layout: building standard Unix directory structure...");

    /* ===== 根级目录 ===== */
    TRY_MKDIR("/bin");
    TRY_MKDIR("/sbin");
    TRY_MKDIR("/boot");
    TRY_MKDIR("/dev");
    TRY_MKDIR("/etc");
    TRY_MKDIR("/lib");
    TRY_MKDIR("/mnt");
    TRY_MKDIR("/opt");
    TRY_MKDIR("/proc");
    TRY_MKDIR("/sys");
    TRY_MKDIR("/tmp");

    /* ===== 用户家目录 ===== */
    TRY_MKDIR("/root");
    TRY_MKDIR("/home");
    TRY_MKDIR("/home/admin");
    TRY_MKDIR("/home/guest");

    /* ===== /usr 层级 ===== */
    TRY_MKDIR("/usr");
    TRY_MKDIR("/usr/bin");
    TRY_MKDIR("/usr/sbin");
    TRY_MKDIR("/usr/lib");
    TRY_MKDIR("/usr/share");
    TRY_MKDIR("/usr/include");
    TRY_MKDIR("/usr/local");
    TRY_MKDIR("/usr/local/bin");

    /* ===== /var 层级 ===== */
    TRY_MKDIR("/var");
    TRY_MKDIR("/var/log");
    TRY_MKDIR("/var/run");
    TRY_MKDIR("/var/tmp");
    TRY_MKDIR("/var/cache");
    TRY_MKDIR("/var/db");
    TRY_MKDIR("/var/spool");
    TRY_MKDIR("/var/mail");

    /* ===== /etc 子目录 ===== */
    TRY_MKDIR("/etc/network");
    TRY_MKDIR("/etc/init.d");
    TRY_MKDIR("/etc/profile.d");

    /* ===== 挂载点 ===== */
    TRY_MKDIR("/mnt/disk");
    TRY_MKDIR("/mnt/usb");
    TRY_MKDIR("/mnt/cdrom");

    klog_info("fs_build_layout: directory structure ready");

    /* ===== 写入系统配置文件 ===== */
    char ver_buf[128];
    int vi = 0;
    const char *vpre = "FunsOS version ";
    while (*vpre) ver_buf[vi++] = *vpre++;
    const char *v = KERNEL_VERSION;
    while (*v && vi < 120) ver_buf[vi++] = *v++;
    const char *vsuf = "\n";
    while (*vsuf) ver_buf[vi++] = *vsuf++;
    ver_buf[vi] = '\0';

    TRY_WRITE("/etc/os-release",
              "NAME=FunsOS\n"
              "ID=funsos\n"
              "PRETTY_NAME=\"FunsOS " KERNEL_VERSION "\"\n"
              "VERSION=\"" KERNEL_VERSION "\"\n"
              "HOME_URL=\"http://funsos.local\"\n", 0644);

    TRY_WRITE("/etc/hostname", "funsos\n", 0644);
    TRY_WRITE("/etc/version", ver_buf, 0444);

    TRY_WRITE("/etc/passwd",
              "root:x:0:0:root:/root:/bin/sh\n"
              "sover:x:0:0:Sover:/root:/bin/sh\n"
              "admin:x:1000:1000:Admin:/home/admin:/bin/sh\n"
              "guest:x:1001:1001:Guest:/home/guest:/bin/sh\n", 0644);

    TRY_WRITE("/etc/group",
              "root:x:0:\n"
              "wheel:x:0:root,sover\n"
              "users:x:100:\n"
              "admin:x:1000:admin\n"
              "guest:x:1001:guest\n", 0644);

    TRY_WRITE("/etc/hosts",
              "127.0.0.1  localhost funsos\n"
              "::1        localhost ip6-localhost\n", 0644);

    TRY_WRITE("/etc/resolv.conf",
              "# FunsOS DNS resolver configuration\n"
              "nameserver 8.8.8.8\n"
              "nameserver 1.1.1.1\n", 0644);

    TRY_WRITE("/etc/profile",
              "# FunsOS system profile\n"
              "export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin\n"
              "export PS1=\"\\u@\\h:\\w\\$ \"\n"
              "export USER=sover\n"
              "export HOME=/root\n"
              "export SHELL=/bin/sh\n", 0644);

    TRY_WRITE("/etc/motd",
              "\n  Welcome to FunsOS " KERNEL_VERSION "!\n"
              "  Type 'help' for available commands.\n\n", 0644);

    TRY_WRITE("/etc/shells",
              "/bin/sh\n"
              "/bin/bash\n", 0644);

    TRY_WRITE("/etc/fstab",
              "# /etc/fstab - FunsOS filesystem table\n"
              "# <device>  <mount>  <type>  <options>  <dump>  <pass>\n"
              "ramfs       /        ramfs   defaults   0       0\n"
              "devtmpfs    /dev     devfs   defaults   0       0\n", 0644);

    TRY_WRITE("/root/.profile",
              "# Root user profile\n"
              "alias ll='ls -l'\n"
              "alias la='ls -la'\n", 0644);

    TRY_WRITE("/etc/issue",
              "FunsOS " KERNEL_VERSION " (tty1)\n\n", 0644);

    klog_info("fs_build_layout: system configuration files written");

    fs_run_self_tests();
}
