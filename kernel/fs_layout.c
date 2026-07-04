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
    TRY_MKDIR("/lib64");
    TRY_MKDIR("/mnt");
    TRY_MKDIR("/opt");
    TRY_MKDIR("/proc");
    TRY_MKDIR("/sys");
    TRY_MKDIR("/tmp");
    TRY_MKDIR("/run");
    TRY_MKDIR("/srv");
    TRY_MKDIR("/media");
    TRY_MKDIR("/lost+found");

    /* ===== /system - 系统核心目录 ===== */
    TRY_MKDIR("/system");
    TRY_MKDIR("/system/kernel");
    TRY_MKDIR("/system/drivers");
    TRY_MKDIR("/system/modules");
    TRY_MKDIR("/system/firmware");
    TRY_MKDIR("/system/config");
    TRY_MKDIR("/system/logs");
    TRY_MKDIR("/system/bin");
    TRY_MKDIR("/system/lib");

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
    TRY_MKDIR("/usr/lib64");
    TRY_MKDIR("/usr/share");
    TRY_MKDIR("/usr/include");
    TRY_MKDIR("/usr/local");
    TRY_MKDIR("/usr/local/bin");
    TRY_MKDIR("/usr/local/sbin");
    TRY_MKDIR("/usr/local/lib");
    TRY_MKDIR("/usr/local/etc");
    TRY_MKDIR("/usr/games");
    TRY_MKDIR("/usr/src");
    TRY_MKDIR("/usr/libexec");

    /* ===== /usr/share 子目录 ===== */
    TRY_MKDIR("/usr/share/doc");
    TRY_MKDIR("/usr/share/man");
    TRY_MKDIR("/usr/share/man/man1");
    TRY_MKDIR("/usr/share/man/man2");
    TRY_MKDIR("/usr/share/man/man3");
    TRY_MKDIR("/usr/share/man/man4");
    TRY_MKDIR("/usr/share/man/man5");
    TRY_MKDIR("/usr/share/man/man6");
    TRY_MKDIR("/usr/share/man/man7");
    TRY_MKDIR("/usr/share/man/man8");
    TRY_MKDIR("/usr/share/info");
    TRY_MKDIR("/usr/share/locale");
    TRY_MKDIR("/usr/share/zoneinfo");
    TRY_MKDIR("/usr/share/misc");
    TRY_MKDIR("/usr/share/terminfo");

    /* ===== /var 层级 ===== */
    TRY_MKDIR("/var");
    TRY_MKDIR("/var/log");
    TRY_MKDIR("/var/run");
    TRY_MKDIR("/var/tmp");
    TRY_MKDIR("/var/cache");
    TRY_MKDIR("/var/db");
    TRY_MKDIR("/var/spool");
    TRY_MKDIR("/var/mail");
    TRY_MKDIR("/var/lib");
    TRY_MKDIR("/var/lib/dpkg");
    TRY_MKDIR("/var/lib/rpm");
    TRY_MKDIR("/var/lock");
    TRY_MKDIR("/var/opt");
    TRY_MKDIR("/var/local");
    TRY_MKDIR("/var/account");
    TRY_MKDIR("/var/crash");
    TRY_MKDIR("/var/games");
    TRY_MKDIR("/var/yp");

    /* ===== /etc 子目录 ===== */
    TRY_MKDIR("/etc/network");
    TRY_MKDIR("/etc/init.d");
    TRY_MKDIR("/etc/profile.d");
    TRY_MKDIR("/etc/rc.d");
    TRY_MKDIR("/etc/rc.d/init.d");
    TRY_MKDIR("/etc/sysconfig");
    TRY_MKDIR("/etc/default");
    TRY_MKDIR("/etc/cron.d");
    TRY_MKDIR("/etc/cron.daily");
    TRY_MKDIR("/etc/cron.hourly");
    TRY_MKDIR("/etc/cron.weekly");
    TRY_MKDIR("/etc/cron.monthly");
    TRY_MKDIR("/etc/security");
    TRY_MKDIR("/etc/pam.d");
    TRY_MKDIR("/etc/ssh");
    TRY_MKDIR("/etc/ssl");
    TRY_MKDIR("/etc/ssl/certs");
    TRY_MKDIR("/etc/ssl/private");
    TRY_MKDIR("/etc/X11");
    TRY_MKDIR("/etc/xdg");
    TRY_MKDIR("/etc/sound");
    TRY_MKDIR("/etc/modules-load.d");
    TRY_MKDIR("/etc/sysctl.d");
    TRY_MKDIR("/etc/tmpfiles.d");
    TRY_MKDIR("/etc/udev");
    TRY_MKDIR("/etc/udev/rules.d");
    TRY_MKDIR("/etc/logrotate.d");

    /* ===== /lib 子目录 ===== */
    TRY_MKDIR("/lib/modules");
    TRY_MKDIR("/lib/firmware");
    TRY_MKDIR("/lib/udev");
    TRY_MKDIR("/lib/security");

    /* ===== /boot 子目录 ===== */
    TRY_MKDIR("/boot/grub");
    TRY_MKDIR("/boot/efi");

    /* ===== /proc 模拟文件 ===== */
    TRY_MKDIR("/proc/net");
    TRY_MKDIR("/proc/sys");
    TRY_MKDIR("/proc/sys/kernel");
    TRY_MKDIR("/proc/sys/net");
    TRY_MKDIR("/proc/sys/vm");
    TRY_MKDIR("/proc/sys/fs");

    /* ===== /sys 模拟文件 ===== */
    TRY_MKDIR("/sys/devices");
    TRY_MKDIR("/sys/devices/system");
    TRY_MKDIR("/sys/devices/virtual");
    TRY_MKDIR("/sys/block");
    TRY_MKDIR("/sys/class");
    TRY_MKDIR("/sys/bus");
    TRY_MKDIR("/sys/module");
    TRY_MKDIR("/sys/kernel");
    TRY_MKDIR("/sys/fs");

    /* ===== 挂载点 ===== */
    TRY_MKDIR("/mnt/disk");
    TRY_MKDIR("/mnt/usb");
    TRY_MKDIR("/mnt/cdrom");
    TRY_MKDIR("/mnt/floppy");
    TRY_MKDIR("/media/cdrom");
    TRY_MKDIR("/media/usb");
    TRY_MKDIR("/media/floppy");

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

    /* ===== 内核与系统文件 ===== */

    /* /boot - 内核镜像 */
    TRY_WRITE("/boot/vmlinuz-" KERNEL_VERSION,
              "FunsOS kernel image " KERNEL_VERSION "\n"
              "built: " __DATE__ " " __TIME__ "\n"
              "arch: i386\n"
              "type: monolithic\n", 0644);

    TRY_WRITE("/boot/System.map-" KERNEL_VERSION,
              "System.map for FunsOS " KERNEL_VERSION "\n"
              "00100000 T _start\n"
              "00101000 T kernel_main\n"
              "00200000 T vfs_init\n"
              "00300000 T pmm_init\n"
              "00400000 T scheduler_init\n", 0644);

    TRY_WRITE("/boot/config-" KERNEL_VERSION,
              "# FunsOS kernel configuration\n"
              "CONFIG_X86=y\n"
              "CONFIG_VGA_TEXT=y\n"
              "CONFIG_RAMFS=y\n"
              "CONFIG_EXT2=y\n"
              "CONFIG_EXT4=y\n"
              "CONFIG_BTRFS=y\n"
              "CONFIG_XFS=y\n"
              "CONFIG_PROCFS=y\n"
              "CONFIG_SYSFS=y\n"
              "CONFIG_DEVFS=y\n"
              "CONFIG_NET=y\n"
              "CONFIG_TCP=y\n"
              "CONFIG_UDP=y\n"
              "CONFIG_SHELL=y\n", 0644);

    TRY_WRITE("/boot/grub/grub.cfg",
              "# GRUB configuration for FunsOS\n"
              "set timeout=5\n"
              "set default=0\n"
              "\n"
              "menuentry 'FunsOS " KERNEL_VERSION "' {\n"
              "    linux /boot/vmlinuz-" KERNEL_VERSION " root=ramfs rw\n"
              "}\n"
              "\n"
              "menuentry 'FunsOS (recovery mode)' {\n"
              "    linux /boot/vmlinuz-" KERNEL_VERSION " root=ramfs rw single\n"
              "}\n", 0644);

    /* /system/kernel - 内核核心文件 */
    TRY_WRITE("/system/kernel/kernel.bin",
              "FunsOS kernel binary\n"
              "version: " KERNEL_VERSION "\n"
              "entry: 0x100000\n"
              "size: 4096KB\n", 0644);

    TRY_WRITE("/system/kernel/ksyms",
              "Kernel symbol table\n"
              "0x00100000 _start\n"
              "0x00101000 kernel_main\n"
              "0x00102000 klog_info\n"
              "0x00103000 klog_warn\n"
              "0x00200000 vfs_init\n"
              "0x00201000 vfs_mount\n"
              "0x00202000 vfs_open\n"
              "0x00300000 pmm_init\n"
              "0x00301000 pmm_alloc\n"
              "0x00302000 pmm_free\n", 0444);

    TRY_WRITE("/system/kernel/kconfig",
              "# Kernel build configuration\n"
              "CONFIG_KERNEL_VERSION=\"" KERNEL_VERSION "\"\n"
              "CONFIG_PAGE_SIZE=4096\n"
              "CONFIG_MAX_PROCESSES=256\n"
              "CONFIG_MAX_FILES=1024\n"
              "CONFIG_STACK_SIZE=8192\n", 0644);

    /* /system/drivers - 系统驱动 */
    TRY_WRITE("/system/drivers/vga_text.ko",
              "VGA text mode driver\n"
              "version: 1.0\n"
              "author: FunsOS Team\n", 0644);

    TRY_WRITE("/system/drivers/keyboard.ko",
              "PS/2 keyboard driver\n"
              "version: 1.0\n", 0644);

    TRY_WRITE("/system/drivers/timer.ko",
              "PIT timer driver\n"
              "version: 1.0\n", 0644);

    TRY_WRITE("/system/drivers/rtc.ko",
              "Real-time clock driver\n"
              "version: 1.0\n", 0644);

    TRY_WRITE("/system/drivers/ide.ko",
              "IDE/ATA disk driver\n"
              "version: 1.0\n", 0644);

    TRY_WRITE("/system/drivers/ne2000.ko",
              "NE2000 network driver\n"
              "version: 1.0\n", 0644);

    /* /system/modules - 内核模块 */
    TRY_WRITE("/system/modules/modules.dep",
              "# Module dependencies\n"
              "vga_text.ko:\n"
              "keyboard.ko:\n"
              "timer.ko:\n"
              "rtc.ko:\n"
              "ide.ko:\n"
              "ne2000.ko:\n", 0644);

    TRY_WRITE("/system/modules/modules.alias",
              "# Module aliases\n"
              "alias pnp:dPNP0303* keyboard\n"
              "alias pnp:dPNP0100* timer\n"
              "alias pnp:dPNP0b00* rtc\n", 0644);

    /* /system/firmware - 固件文件 */
    TRY_WRITE("/system/firmware/README",
              "Firmware files for various devices\n"
              "Place device firmware blobs here.\n", 0644);

    /* /system/config - 系统配置 */
    TRY_WRITE("/system/config/system.conf",
              "# FunsOS system configuration\n"
              "[system]\n"
              "hostname = funsos\n"
              "timezone = UTC\n"
              "language = en_US\n"
              "\n"
              "[kernel]\n"
              "loglevel = 4\n"
              "panic_timeout = 0\n", 0644);

    TRY_WRITE("/system/config/drivers.conf",
              "# Driver configuration\n"
              "[vga_text]\n"
              "enabled = true\n"
              "columns = 80\n"
              "rows = 25\n"
              "\n"
              "[keyboard]\n"
              "enabled = true\n"
              "layout = us\n", 0644);

    /* /system/logs - 系统日志 */
    TRY_WRITE("/system/logs/boot.log",
              "Boot log for FunsOS " KERNEL_VERSION "\n"
              "[    0.000] Booting FunsOS...\n"
              "[    0.001] Initializing memory manager...\n"
              "[    0.002] Initializing VFS...\n"
              "[    0.003] Mounting root filesystem...\n"
              "[    0.005] Starting init process...\n"
              "[    0.010] System ready.\n", 0644);

    TRY_WRITE("/system/logs/kernel.log",
              "Kernel log buffer\n"
              "<6>FunsOS version " KERNEL_VERSION "\n"
              "<6>Memory: 64MB available\n"
              "<6>VFS: mounted ramfs on /\n"
              "<6>NET: TCP/IP stack initialized\n", 0644);

    /* /lib/modules - 内核模块目录 */
    TRY_WRITE("/lib/modules/" KERNEL_VERSION "/modules.dep",
              "# " KERNEL_VERSION " module dependencies\n"
              "kernel/drivers/vga_text.ko:\n"
              "kernel/drivers/keyboard.ko:\n", 0644);

    TRY_WRITE("/lib/modules/" KERNEL_VERSION "/modules.builtin",
              "# Built-in modules\n"
              "kernel/fs/ramfs/ramfs.ko\n"
              "kernel/fs/procfs/procfs.ko\n"
              "kernel/fs/sysfs/sysfs.ko\n"
              "kernel/fs/devfs/devfs.ko\n", 0644);

    /* /proc 模拟文件 */
    TRY_WRITE("/proc/version",
              "FunsOS version " KERNEL_VERSION " (gcc) #1 SMP " __DATE__ "\n", 0444);

    TRY_WRITE("/proc/cpuinfo",
              "processor   : 0\n"
              "vendor_id   : GenuineIntel\n"
              "cpu family  : 6\n"
              "model       : 142\n"
              "model name  : FunsOS Virtual CPU\n"
              "stepping    : 9\n"
              "cpu MHz     : 3000.000\n"
              "cache size  : 8192 KB\n"
              "flags       : fpu vme de pse tsc msr pae mce cx8 apic\n", 0444);

    TRY_WRITE("/proc/meminfo",
              "MemTotal:        65536 kB\n"
              "MemFree:         32768 kB\n"
              "MemAvailable:    49152 kB\n"
              "Buffers:          4096 kB\n"
              "Cached:          12288 kB\n"
              "SwapTotal:           0 kB\n"
              "SwapFree:            0 kB\n", 0444);

    TRY_WRITE("/proc/uptime",
              "0.00 0.00\n", 0444);

    TRY_WRITE("/proc/loadavg",
              "0.00 0.00 0.00 0/0 0\n", 0444);

    TRY_WRITE("/proc/sys/kernel/ostype", "FunsOS\n", 0444);
    TRY_WRITE("/proc/sys/kernel/osrelease", KERNEL_VERSION "\n", 0444);
    TRY_WRITE("/proc/sys/kernel/version", "#1 SMP " __DATE__ "\n", 0444);
    TRY_WRITE("/proc/sys/kernel/hostname", "funsos\n", 0644);

    /* /sys 模拟文件 */
    TRY_WRITE("/sys/kernel/uevent_seqnum", "0\n", 0444);

    /* 更多 /etc 配置文件 */
    TRY_WRITE("/etc/services",
              "# Network services\n"
              "echo            7/tcp\n"
              "echo            7/udp\n"
              "discard         9/tcp    sink null\n"
              "discard         9/udp    sink null\n"
              "ftp            21/tcp\n"
              "ssh            22/tcp\n"
              "telnet         23/tcp\n"
              "smtp           25/tcp\n"
              "domain         53/tcp    nameserver\n"
              "domain         53/udp    nameserver\n"
              "http           80/tcp    www www-http\n"
              "pop3          110/tcp\n"
              "imap          143/tcp\n"
              "https         443/tcp\n", 0644);

    TRY_WRITE("/etc/protocols",
              "# Internet protocols\n"
              "ip      0       IP              # internet protocol\n"
              "icmp    1       ICMP            # internet control message protocol\n"
              "tcp     6       TCP             # transmission control protocol\n"
              "udp     17      UDP             # user datagram protocol\n", 0644);

    TRY_WRITE("/etc/nsswitch.conf",
              "# Name Service Switch configuration\n"
              "passwd:     files\n"
              "group:      files\n"
              "shadow:     files\n"
              "hosts:      files dns\n"
              "networks:   files\n"
              "protocols:  files\n"
              "services:   files\n", 0644);

    TRY_WRITE("/etc/login.defs",
              "# Login configuration\n"
              "PASS_MIN_LEN  5\n"
              "PASS_MAX_DAYS  99999\n"
              "UID_MIN  1000\n"
              "UID_MAX  60000\n"
              "GID_MIN  1000\n"
              "GID_MAX  60000\n", 0644);

    TRY_WRITE("/etc/inittab",
              "# FunsOS inittab\n"
              "id:3:initdefault:\n"
              "si::sysinit:/etc/init.d/rcS\n"
              "l0:0:wait:/sbin/halt\n"
              "l6:6:wait:/sbin/reboot\n"
              "1:2345:respawn:/sbin/getty tty1\n"
              "2:2345:respawn:/sbin/getty tty2\n", 0644);

    TRY_WRITE("/etc/mtab",
              "ramfs / ramfs rw 0 0\n"
              "devtmpfs /dev devfs rw 0 0\n"
              "proc /proc proc rw 0 0\n"
              "sysfs /sys sysfs rw 0 0\n", 0644);

    TRY_WRITE("/etc/shadow",
              "root:!:19000:0:99999:7:::\n"
              "admin:!:19000:0:99999:7:::\n"
              "guest:!:19000:0:99999:7:::\n", 0600);

    TRY_WRITE("/etc/gshadow",
              "root:x::root,sover\n"
              "wheel:x::root,sover\n"
              "users:x::\n", 0640);

    TRY_WRITE("/etc/inputrc",
              "# Inputrc configuration\n"
              "set bell-style none\n"
              "set show-all-if-ambiguous on\n", 0644);

    TRY_WRITE("/etc/sysctl.conf",
              "# Kernel parameters\n"
              "vm.swappiness = 60\n"
              "net.ipv4.ip_forward = 0\n", 0644);

    TRY_WRITE("/etc/ld.so.conf",
              "# Library search path\n"
              "/lib\n"
              "/usr/lib\n"
              "/usr/local/lib\n", 0644);

    TRY_WRITE("/etc/rc.local",
              "#!/bin/sh\n"
              "# Local startup script\n"
              "exit 0\n", 0755);

    /* /var 目录文件 */
    TRY_WRITE("/var/log/messages", "", 0644);
    TRY_WRITE("/var/log/syslog", "", 0644);
    TRY_WRITE("/var/log/auth.log", "", 0644);
    TRY_WRITE("/var/log/dmesg", "", 0644);
    TRY_WRITE("/var/log/lastlog", "", 0644);
    TRY_WRITE("/var/log/wtmp", "", 0644);
    TRY_WRITE("/var/run/utmp", "", 0644);

    TRY_WRITE("/var/lib/dpkg/status",
              "Package: funsos-base\n"
              "Version: " KERNEL_VERSION "\n"
              "Status: install ok installed\n"
              "Description: FunsOS base system\n", 0644);

    /* /usr/share 文件 */
    TRY_WRITE("/usr/share/dict/words",
              "FunsOS\n"
              "kernel\n"
              "system\n"
              "file\n"
              "process\n"
              "memory\n"
              "device\n", 0644);

    TRY_WRITE("/usr/share/misc/magic",
              "# Magic numbers\n"
              "0 string \\x7fELF ELF\n", 0644);

    TRY_WRITE("/usr/share/zoneinfo/UTC",
              "TZif2UTC\n", 0644);

    /* /usr/src - 内核源码 */
    TRY_MKDIR("/usr/src/linux-" KERNEL_VERSION);
    TRY_WRITE("/usr/src/linux-" KERNEL_VERSION "/Makefile",
              "VERSION = 1\n"
              "PATCHLEVEL = 0\n"
              "SUBLEVEL = 0\n"
              "EXTRAVERSION = \n"
              "NAME = FunsOS\n", 0644);

    klog_info("fs_build_layout: kernel and system files written");

    fs_run_self_tests();
}
