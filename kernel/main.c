#include "stdint.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "timer.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "sched.h"
#include "process.h"
#include "syscall.h"
#include "panic.h"
#include "io.h"
#include "net.h"
#include "tcp.h"
#include "udp.h"
#include "ip.h"
#include "icmp.h"
#include "arp.h"
#include "socket.h"
#include "fw.h"
#include "fw_bandwidth.h"
#include "stddef.h"
#include "version.h"
#include "fpu.h"
#include "vfs.h"
#include "quota.h"
#include "ramfs.h"
#include "krng.h"
#include "iosched.h"
#include "softirq.h"
#include "sysrq.h"
#include "oom_killer.h"
#include "sysctl.h"
#include "devfs.h"
#include "procfs.h"
#include "sysfs.h"
#include "tmpfs.h"
#include "initrd.h"
#include "vesa.h"
#include "fb_console.h"
#include "shell.h"
#include "keyboard.h"
#include "mouse.h"
#include "rtc.h"
#include "boot_info.h"
#include "driver_manager.h"
#include "disk_manager.h"
#include "drm.h"
#include "i915.h"
#include "acpi_sleep.h"
#include "cpufreq.h"
#include "cpuidle.h"
#include "regmap.h"
#include "hwmon.h"
#include "ftrace.h"
#include "dmabuf.h"
#include "iio.h"
#include "pwm.h"
#include "led.h"
#include "pinctrl.h"
#include "gpio.h"
#include "dmaengine.h"
#include "clk.h"
#include "i2c.h"
#include "spi.h"
#include "mfd.h"
#include "battery.h"
#include "klog.h"
#include "syslog.h"
#include "tarfs.h"
#include "http_client.h"
#include "pkgmgr.h"
#include "user.h"
#include "fs_layout.h"

#include "ksym.h"
#include "kdebug.h"
#include "kmodule.h"
#include "perf.h"
#include "knotifier.h"
#include "health.h"
#include "ktrace.h"
#include "kwork.h"
#include "registry.h"
#include "cron.h"
#include "evlog.h"
#include "fim.h"
#include "appexec.h"
#include "netmon.h"
#include "sysacct.h"
#include "svcmgr.h"
#include "taskmgr.h"
#include "crashdump.h"
#include "ipc_sem.h"
#include "signal_diag.h"
#include "quota_db.h"
#include "logrotate_ext.h"
#include "sound.h"
#include "e1000e.h"
#include "ixgbe.h"
#include "rtl8139_ex.h"
#include "dm9000.h"
#include "wifi_stub.h"

#include "gui_core.h"
#include "pci_bus.h"
#include "amdgpu.h"
#include "ipv6.h"
#include "fun_format.h"
#include "vfs_ext.h"
#include "user_ext.h"
#include "env.h"
#include "rlimit.h"
#include "page_replace.h"
#include "workqueue.h"
#include "rcu.h"
#include "hrtimer.h"
#include "slab.h"
#include "watchdog.h"
#include "knotifier.h"
#include "crypto.h"
#include "vmalloc.h"
#include "percpu.h"
#include "kfence.h"
#include "debugobjects.h"
#include "lockdep.h"
#include "irqdomain.h"

#include "devtmpfs.h"
#include "ksysfs.h"
#include "netns.h"
#include "knetfilter.h"
#include "seccomp.h"
#include "apparmor.h"
#include "keyring.h"
#include "audit.h"

#include "namespace.h"
#include "tracepoint.h"
#include "uprobe.h"
#include "kmod.h"
#include "firmware.h"
#include "remoteproc.h"
#include "rpmsg.h"
#include "virtio.h"

#include "vga_text.h"
#include "serial.h"
#include "service_registry.h"
#include "splash.h"
#include "app_registry.h"
#include "path.h"
#include "dentry.h"
#include "string.h"
#include "printf_test.h"

static inline void sti(void) {
    asm volatile("sti");
}

static inline void hlt(void) {
    asm volatile("hlt");
}

static void init_create_file(const char *path, const char *content) {
    dentry_t *dir_dentry = NULL;
    char dir_path[512], name[256];
    strncpy(dir_path, path, 511); dir_path[511] = '\0';
    char *slash = strrchr(dir_path, '/');
    if (!slash) return;
    strncpy(name, slash + 1, 255); name[255] = '\0';
    if (slash == dir_path) {
        dir_path[1] = '\0';
    } else {
        *slash = '\0';
    }
    if (path_resolve(dir_path, &dir_dentry) != 0 || !dir_dentry ||
        !dir_dentry->inode || !dir_dentry->inode->ops ||
        !dir_dentry->inode->ops->create) {
        return;
    }
    dir_dentry->inode->ops->create(dir_dentry, name, FILE_MODE_READ | FILE_MODE_WRITE);
    if (!content || !*content) return;
    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_READ | FILE_MODE_WRITE, &f) != 0 || !f) return;
    vfs_write(f, content, (uint32_t)strlen(content));
    vfs_close(f);
}

static void init_mkdir(const char *path) {
    char parent_path[512], name[256];
    strncpy(parent_path, path, 511); parent_path[511] = '\0';
    char *slash = strrchr(parent_path, '/');
    if (!slash) return;
    strncpy(name, slash + 1, 255); name[255] = '\0';
    if (slash == parent_path) {
        parent_path[1] = '\0';
    } else {
        *slash = '\0';
    }
    dentry_t *dir_dentry = NULL;
    if (path_resolve(parent_path, &dir_dentry) != 0 || !dir_dentry ||
        !dir_dentry->inode || !dir_dentry->inode->ops ||
        !dir_dentry->inode->ops->mkdir) {
        return;
    }
    dir_dentry->inode->ops->mkdir(dir_dentry, name, FILE_MODE_READ | FILE_MODE_WRITE | FILE_MODE_DIR);
}

static void init_root_files(void) {
    init_mkdir("/home");
    init_mkdir("/home/user");
    init_mkdir("/docs");
    init_mkdir("/tmp");
    init_mkdir("/etc");

    init_create_file("/welcome.txt",
        "====================================================\n"
        "  Welcome to FunsOS v0.5\n"
        "====================================================\n"
        "\n"
        "  This is a hobby operating system written in C.\n"
        "  Features:\n"
        "    - Full TCP/IP network stack\n"
        "    - RAMFS root file system\n"
        "    - Multiple file system support (EXT2/3/4, FAT32, etc.)\n"
        "    - Command-line shell with 60+ built-in commands\n"
        "    - FunRender graphics engine\n"
        "\n"
        "  Quick commands:\n"
        "    help     - Show all commands\n"
        "    ls       - List directory contents\n"
        "    cat      - Show file contents\n"
        "    ifconfig - Show network interfaces\n"
        "    ping     - Test network connectivity\n"
        "\n"
        "  Have fun!\n"
        "====================================================\n");

    init_create_file("/README.txt",
        "FunsOS - Fun Operating System\n"
        "=============================\n"
        "\n"
        "About\n"
        "-----\n"
        "FunsOS is a 32-bit x86 hobby OS written from scratch in C.\n"
        "It boots via GRUB-compatible bootloader and runs in protected mode.\n"
        "\n"
        "Directory Structure\n"
        "-------------------\n"
        "  /boot/    - Boot files\n"
        "  /etc/     - System configuration\n"
        "  /home/    - User directories\n"
        "  /docs/    - Documentation\n"
        "  /tmp/     - Temporary files\n"
        "  /dev/     - Device files (devfs)\n"
        "\n"
        "Network Commands\n"
        "----------------\n"
        "  ifconfig   - Display network interfaces\n"
        "  ping <ip>  - Send ICMP echo requests\n"
        "  route      - Show routing table\n"
        "  dns <host> - DNS lookup\n"
        "  wget <url> - Download file via HTTP\n"
        "  netstat    - Show network statistics\n"
        "  arp        - Show ARP table\n"
        "\n"
        "File System Commands\n"
        "--------------------\n"
        "  ls / dir   - List files\n"
        "  cd / go    - Change directory\n"
        "  pwd / where- Print working directory\n"
        "  cat / type - Display file contents\n"
        "  touch      - Create empty file\n"
        "  mkdir      - Create directory\n"
        "  cp / copy  - Copy files\n"
        "  rm / del   - Delete files\n"
        "  mv / ren   - Move/rename files\n"
        "\n");

    init_create_file("/docs/network.txt",
        "FunsOS Networking Guide\n"
        "========================\n"
        "\n"
        "Supported Network Drivers\n"
        "-------------------------\n"
        "  - Intel E1000 / E1000e (i219, i225)\n"
        "  - Realtek RTL8139 / RTL8169\n"
        "  - AMD PCnet32 (pcnet)\n"
        "  - NE2000 compatible (ne2k_pci)\n"
        "  - VirtIO net (virtio-net)\n"
        "  - Mellanox ConnectX-3 (cx3)\n"
        "  - Broadcom BCM57xx (b57)\n"
        "  - Davicom DM9000\n"
        "\n"
        "Protocol Stack\n"
        "--------------\n"
        "  - Ethernet / ARP\n"
        "  - IPv4 / IPv6\n"
        "  - ICMP / IGMP\n"
        "  - TCP / UDP / UDP-Lite\n"
        "  - DNS / DHCP / NTP\n"
        "  - HTTP client/server\n"
        "  - FTP server / Telnet / TFTP\n"
        "  - TCP congestion control\n"
        "  - Netfilter / Firewall / NAT\n"
        "\n"
        "Loopback Test\n"
        "-------------\n"
        "The loopback interface (lo, 127.0.0.1) is always available.\n"
        "Test it with: ping 127.0.0.1\n"
        "\n");

    init_create_file("/docs/commands.txt",
        "FunsOS Shell Command Reference\n"
        "===============================\n"
        "\n"
        "Navigation:\n"
        "  ls, dir, pt   List directory contents\n"
        "  cd, go        Change directory\n"
        "  pwd, where    Print working directory\n"
        "  tree          Show directory tree\n"
        "\n"
        "File Operations:\n"
        "  cat, type     Display file contents\n"
        "  touch         Create empty file\n"
        "  mkdir         Create directory\n"
        "  cp, copy      Copy files\n"
        "  rm, del       Delete files\n"
        "  mv, ren       Move/rename files\n"
        "  append        Append text to file\n"
        "  head          Show first lines of file\n"
        "  tail          Show last lines of file\n"
        "  wc            Word/line/character count\n"
        "  grep          Search text in file\n"
        "  sort          Sort file lines\n"
        "  uniq          Remove duplicate lines\n"
        "  diff          Compare two files\n"
        "  stat          Show file status\n"
        "  chmod         Change file mode\n"
        "  chown         Change file owner\n"
        "  ln, ln_s      Create links\n"
        "\n"
        "System:\n"
        "  ver           Show OS version\n"
        "  sysinfo       System information\n"
        "  mem / free    Memory usage\n"
        "  ps            Process list\n"
        "  top           System monitor\n"
        "  uptime        System uptime\n"
        "  dev           Device list\n"
        "  dmesg         Kernel messages\n"
        "  date / time   Date and time\n"
        "  reboot / halt Restart / shutdown\n"
        "\n"
        "Network:\n"
        "  ifconfig      Network interfaces\n"
        "  ping          ICMP echo\n"
        "  route         Routing table\n"
        "  dns           DNS lookup\n"
        "  wget          HTTP download\n"
        "  netstat       Network stats\n"
        "  arp           ARP table\n"
        "  traceroute    Trace route\n"
        "\n");

    init_create_file("/etc/hostname", "funsos\n");
    init_create_file("/etc/hosts",
        "127.0.0.1   localhost funsos\n"
        "::1         localhost\n");

    init_create_file("/home/user/notes.txt",
        "User Notes\n"
        "==========\n"
        "\n"
        "Welcome to your home directory!\n"
        "This is a great place to store your files.\n"
        "\n"
        "Things to try:\n"
        "  1. echo \"Hello World\" > hello.txt\n"
        "  2. cat hello.txt\n"
        "  3. ls -la /\n"
        "  4. ping 127.0.0.1\n"
        "\n");

    init_create_file("/tmp/readme.txt",
        "Temporary directory\n"
        "===================\n"
        "\n"
        "Files in this directory may be deleted on reboot.\n");

    /* ---- Code examples ---- */
    init_mkdir("/src");
    init_create_file("/src/hello.c",
        "#include <stdio.h>\n"
        "\n"
        "int main(void) {\n"
        "    printf(\"Hello, World!\\n\");\n"
        "    return 0;\n"
        "}\n");

    init_create_file("/src/hello.h",
        "#ifndef HELLO_H\n"
        "#define HELLO_H\n"
        "\n"
        "void say_hello(void);\n"
        "\n"
        "#endif\n");

    init_create_file("/src/main.py",
        "#!/usr/bin/env python3\n"
        "\n"
        "def main():\n"
        "    print(\"Hello from Python!\")\n"
        "    for i in range(5):\n"
        "        print(f\"Count: {i}\")\n"
        "\n"
        "if __name__ == \"__main__\":\n"
        "    main()\n");

    init_create_file("/src/boot.asm",
        "; Simple boot sector example\n"
        "org 0x7c00\n"
        "\n"
        "start:\n"
        "    mov ah, 0x0e\n"
        "    mov al, 'H'\n"
        "    int 0x10\n"
        "    jmp $\n"
        "\n"
        "times 510-($-$$) db 0\n"
        "dw 0xaa55\n");

    init_create_file("/src/Makefile",
        "CC = gcc\n"
        "CFLAGS = -Wall -Wextra -O2\n"
        "\n"
        "all: hello\n"
        "\n"
        "hello: hello.c\n"
        "\t$(CC) $(CFLAGS) -o $@ $<\n"
        "\n"
        "clean:\n"
        "\trm -f hello\n"
        "\n"
        ".PHONY: all clean\n");

    /* ---- Web examples ---- */
    init_mkdir("/web");
    init_create_file("/web/index.html",
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Welcome to FunsOS</title>\n"
        "    <link rel=\"stylesheet\" href=\"style.css\">\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Hello from FunsOS!</h1>\n"
        "    <p>This is a sample HTML page.</p>\n"
        "    <script src=\"app.js\"></script>\n"
        "</body>\n"
        "</html>\n");

    init_create_file("/web/style.css",
        "body {\n"
        "    font-family: sans-serif;\n"
        "    background: #f0f0f0;\n"
        "    color: #333;\n"
        "    margin: 40px;\n"
        "}\n"
        "\n"
        "h1 {\n"
        "    color: #0066cc;\n"
        "}\n");

    init_create_file("/web/app.js",
        "// Sample JavaScript\n"
        "document.addEventListener('DOMContentLoaded', function() {\n"
        "    console.log('FunsOS web app loaded');\n"
        "    alert('Welcome to FunsOS!');\n"
        "});\n");

    init_create_file("/web/config.json",
        "{\n"
        "  \"title\": \"FunsOS Web\",\n"
        "  \"version\": \"0.5.0\",\n"
        "  \"features\": [\"network\", \"filesystem\", \"multitasking\"],\n"
        "  \"debug\": true\n"
        "}\n");

    /* ---- Config examples ---- */
    init_create_file("/etc/config.ini",
        "[system]\n"
        "name = FunsOS\n"
        "version = 0.5\n"
        "\n"
        "[network]\n"
        "hostname = funsos\n"
        "dns = 8.8.8.8\n"
        "\n"
        "[gui]\n"
        "theme = default\n"
        "resolution = 1024x768\n");

    init_create_file("/etc/network.conf",
        "# Network configuration\n"
        "interface eth0\n"
        "{\n"
        "    ip = 192.168.1.100\n"
        "    netmask = 255.255.255.0\n"
        "    gateway = 192.168.1.1\n"
        "    dns = 8.8.8.8\n"
        "}\n");

    /* ---- Documents ---- */
    init_create_file("/docs/readme.md",
        "# FunsOS Documentation\n"
        "\n"
        "## Overview\n"
        "\n"
        "FunsOS is a hobby operating system written in C.\n"
        "\n"
        "## Features\n"
        "\n"
        "- **Preemptive multitasking**\n"
        "- **Virtual memory management**\n"
        "- **TCP/IP network stack**\n"
        "- **Multiple file systems**\n"
        "\n"
        "## Quick Start\n"
        "\n"
        "```bash\n"
        "make && make run\n"
        "```\n");

    /* ---- Data files ---- */
    init_mkdir("/data");
    init_create_file("/data/users.csv",
        "id,username,email,created_at\n"
        "1,admin,admin@funsos.local,2024-01-01\n"
        "2,user,user@funsos.local,2024-01-15\n"
        "3,guest,guest@funsos.local,2024-02-01\n");

    init_create_file("/data/settings.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<settings>\n"
        "  <appearance>\n"
        "    <theme>dark</theme>\n"
        "    <font-size>12</font-size>\n"
        "  </appearance>\n"
        "  <behavior>\n"
        "    <autosave>true</autosave>\n"
        "    <notifications>true</notifications>\n"
        "  </behavior>\n"
        "</settings>\n");

    /* ---- Log files ---- */
    init_mkdir("/var");
    init_mkdir("/var/log");
    init_create_file("/var/log/system.log",
        "[00:00:01] SYSTEM: FunsOS v0.5 booting...\n"
        "[00:00:02] SYSTEM: Memory initialized (128MB total)\n"
        "[00:00:03] VFS: Root filesystem mounted (ramfs)\n"
        "[00:00:04] NET: Loopback interface up\n"
        "[00:00:05] SYSTEM: Shell started\n");

    /* ---- Archive placeholders (just metadata) ---- */
    init_create_file("/backup.tar.gz",
        "This is a placeholder for a tar.gz archive.\n"
        "In a real system this would be binary compressed data.\n");

    init_create_file("/image.bmp",
        "BMP image placeholder.\n"
        "This would contain binary pixel data in a real system.\n");
}

void kernel_main(void) {
    /* DBG-0: raw serial port write before anything */
    outb(0x3F8, '0');
    outb(0x3F8, '\r');
    outb(0x3F8, '\n');

    /* DBG-1: serial init */
    serial_init(COM1);
    outb(0x3F8, 'a');
    outb(0x3F8, '\r');
    outb(0x3F8, '\n');
    outb(0x3F8, 'b');
    outb(0x3F8, '\r');
    outb(0x3F8, '\n');
    outb(0x3F8, 'c');
    serial_print(COM1, "[1] serial ok\n");
    outb(0x3F8, 'd');
    outb(0x3F8, '\r');
    outb(0x3F8, '\n');
    outb(0x3F8, 'e');

    init_gdt();
    serial_print(COM1, "[2] gdt ok\n");
    /* Set up TSS kernel stack for interrupt delivery */
    {
        extern void gdt_set_tss(uint32_t ss0, uint32_t esp0);
        uint32_t kern_esp;
        asm volatile("mov %%esp, %0" : "=r"(kern_esp));
        gdt_set_tss(0x10, kern_esp);
    }
    init_idt();
    serial_print(COM1, "[3] idt ok\n");
    init_irq();
    serial_print(COM1, "[4] irq ok\n");
    fpu_init();
    serial_print(COM1, "[5] fpu ok\n");
    init_timer();
    serial_print(COM1, "[6] timer ok\n");

    /* Initialize kernel RNG early - needed for ASLR, PID allocation, etc */
    krng_init();
    serial_print(COM1, "[7] krng ok\n");
    klog_info("Kernel random number generator (xorshift128+) initialized");

    /* Initialize softirq/tasklet subsystem */
    softirq_init();

    /* Initialize I/O scheduler framework */
    iosched_init();

    /* Initialize SysRq magic key system */
    sysrq_init();

    /* Initialize sysctl interface */
    sysctl_init();

    /* Initialize OOM killer */
    oom_init();

    /* Initialize notifier chains */
    knotifier_chain_init();

    /* Initialize RCU */
    rcu_init();

    /* Initialize workqueue */
    workqueue_init();

    /* Initialize high-resolution timers */
    hrtimer_init();

    /* Initialize slab allocator */
    slab_init();

    /* Initialize watchdog */
    watchdog_init();

    /* Initialize crypto API */
    crypto_init();

    /* Initialize kernel log ring buffer early */
    klog_init();
    klog_info("Kernel log initialized");

    /* Initialize extended kernel subsystems */
    vmalloc_init();
    percpu_init();
    kfence_init();
    debug_objects_init();
    lockdep_init();
    irqdomain_init();
    cpufreq_init();
    cpuidle_init();
    regmap_init();
    hwmon_init();
    ftrace_init();
    dmabuf_init();
    iio_init();
    pwm_init();
    led_init();
    pinctrl_init();
    gpio_init();
    dmaengine_init();
    clk_init();
    i2c_init();
    spi_init();
    mfd_init();
    klog_info("Extended kernel subsystems initialized");

    devtmpfs_init();
    ksysfs_init();
    netns_init();
    knetfilter_init();
    seccomp_init();
    apparmor_init();
    keyring_init();
    audit_init();
    klog_info("Filesystem, network and security subsystems initialized");

    /* Namespace isolation subsystem */
    namespace_init();
    klog_info("Namespace isolation initialized");

    /* Static tracepoints */
    tracepoint_init();
    klog_info("Static tracepoints initialized");

    /* User-space probes */
    uprobe_init();
    klog_info("User-space probes initialized");

    /* Kernel module loader */
    kmod_init();
    klog_info("Kernel module loader initialized");

    /* Firmware loader */
    firmware_init();
    klog_info("Firmware loader initialized");

    /* Remote processor framework */
    remoteproc_init();
    klog_info("Remote processor framework initialized");

    /* Remote processor messaging */
    rpmsg_init();
    klog_info("Remote processor messaging initialized");

    /* VirtIO framework */
    virtio_init();
    klog_info("VirtIO framework initialized");

    /* Initialize system environment variables */
    sysenv_init();
    klog_info("Environment variables initialized");

    init_pmm(NULL);  /* NULL => detect from 0x700 boot_info if available */
    /* Try to use bootloader memory info for accurate PMM init.
     * The loader stores a boot_info_t at 0x500, but the magic
     * field is at 0x700.  If valid, pass mem_upper to PMM so it
     * knows the real RAM size instead of assuming 4 GB. */
    {
        boot_info_t bi;
        uint32_t magic = *(volatile uint32_t *)0x700;
        if (magic == BOOT_INFO_MAGIC) {
            bi.mem_upper = *(volatile uint32_t *)0x50C;
            if (bi.mem_upper > 0) {
                /* Re-init PMM with correct memory size */
                init_pmm(&bi);
            }
        }
    }
    klog_info("Physical memory manager initialized");
    init_vmm();
    klog_info("Virtual memory manager initialized");
    kheap_init(0xD0000000, 0x02000000); /* 32MB heap at 0xD0000000 */
    klog_info("Kernel heap initialized");
    sched_init();
    init_process();
    sched_create_idle_task();
    init_syscall();
    klog_info("Scheduler and process subsystem initialized");

    /* Register kernel_main as a proper kernel process so the scheduler
     * can save/restore its context when switching to user processes. */
    {
        extern pcb_t *process_adopt_current(const char *name);
        pcb_t *init_proc = process_adopt_current("init");
        if (init_proc) {
            sched_add(init_proc);
            sched_set_current(init_proc);
            klog_info("Init process registered (pid=%d)", init_proc->pid);
        }
    }

    net_init();
    arp_init();
    ip_init();
    icmp_init();
    udp_init();
    tcp_init();
    socket_init();
    klog_info("Network stack initialized");


    /* 内核符号表 - 调试器和模块加载器依赖 */
    ksym_init();
    klog_info("Kernel symbol table initialized");

    /* 内核模块加载器 */
    kmodule_init();
    klog_info("Kernel module system initialized");

    /* 内核调试器 */
    kdebug_init();
    klog_info("Kernel debugger initialized (Ctrl+Shift+D to enter)");

    /* 性能监控 */
    perf_init();
    klog_info("Performance monitoring initialized");

    /* 系统健康监控 */
    health_init();
    klog_info("System health monitor initialized");

    /* 内核跟踪子系统（结构化事件 ring buffer） */
    ktrace_init();
    klog_info("Kernel tracing subsystem initialized");

    /* 内核工作队列子系统（延迟执行） */
    kwork_init();
    klog_info("Kernel workqueue subsystem initialized");

    /* 系统注册表（基于 FunDB 的层次化配置存储） */
    registry_init();
    klog_info("System registry initialized");

    /* 定时任务调度子系统（基于 kwork + FunDB） */
    cron_init();
    klog_info("Cron scheduler initialized");

    /* 系统事件日志子系统（持久化结构化事件日志，基于 FunDB） */
    evlog_init();
    klog_info("Event log subsystem initialized");
    evlog_info("System", 1, "Event log started, retention=%u",
               EVLOG_DEFAULT_RETENTION);

    /* 文件完整性监视子系统 */
    fim_init();
    klog_info("File integrity monitor initialized");

    /* 应用执行服务 */
    appexec_init();
    klog_info("App execution service initialized");

    /* 网络监视子系统（基于 kwork 周期采样） */
    netmon_init();
    klog_info("Network monitor initialized");

    /* 系统账户审计桥接（SAM：合并 user_ext 审计/锁定/会话到 evlog） */
    sysacct_init();
    klog_info("System accountant (SAM) initialized");

    /* 服务管理器（运行时常驻服务生命周期，FunDB 持久化） */
    svcmgr_init();
    klog_info("Service manager initialized");

    /* 任务管理器（进程统计 + top-N + 审计 kill） */
    taskmgr_init();
    klog_info("Task manager initialized");

    /* 崩溃转储子系统（在 panic 时持久化上下文） */
    crashdump_init();
    klog_info("Crash dump subsystem initialized");

    /* IPC 信号量子系统（补全 System V IPC：msg + shm + sem） */
    ipc_sem_init();
    klog_info("IPC semaphore subsystem initialized");

    /* 信号诊断子系统（统计 + evlog + FunDB 持久化） */
    sigdiag_init();
    klog_info("Signal diagnostics initialized");

    /* 配额持久化子系统（FunDB 持久化 quota.c 的内存表） */
    quota_db_init();
    quota_db_load_all();
    klog_info("Quota persistence initialized and loaded");

    /* 日志轮转扩展（基于 kwork 的自动轮转 + evlog） */
    logrotate_ext_init();
    logrotate_ext_start_auto(LOGROTATE_EXT_DEFAULT_INTERVAL_MS);
    klog_info("Log rotate extension initialized (auto every %u ms)",
              LOGROTATE_EXT_DEFAULT_INTERVAL_MS);

    /* 音频子系统及驱动 */
    sound_init();
    klog_info("Audio subsystem initialized");

    /* 新增网卡驱动探测 */
    {
        extern int e1000e_probe(void);
        extern int ixgbe_probe(void);
        extern int dm9000_probe(void);
        e1000e_probe();
        ixgbe_probe();
        dm9000_probe();
    }
    klog_info("Additional network drivers probed");

    /* Firewall + bandwidth management (stateful inspection, NAT,
     * per-interface rate limiting).  Hooks into the netfilter layer
     * created by net_init() above. */
    fw_init();
    fw_qdisc_init();

    /* Bring up secondary NICs.  pcnet, virtio, rtl8139, e1000 are
     * probed by net_init(); we additionally try the NE2000 (RTL8029)
     * which is exposed by QEMU's -device ne2k_isa / ne2k_pci.
     * Also probe RTL8169, Intel I225-V, Intel I219-V and
     * Mellanox ConnectX-3 NICs. */
    {
        extern int ne2k_probe(void);
        extern int rtl8169_probe(void);
        extern int i225_probe(void);
        extern int i219_probe(void);
        extern int connectx3_probe(void);
        extern int b57_probe(void);
        ne2k_probe();
        rtl8169_probe();
        i225_probe();
        i219_probe();
        connectx3_probe();
        b57_probe();
    }

    vfs_init();
    ramfs_init();
    quota_init();
    klog_info("Root filesystem (ramfs) mounted");

    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/sbin", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/root", 0700);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/usr", 0755);
    vfs_mkdir("/mnt", 0755);
    vfs_mkdir("/opt", 0755);
    klog_info("Standard directories created");

    devfs_init();
    klog_info("devfs mounted on /dev");

    vfs_mkdir("/tmp", 01777);
    tmpfs_init();
    klog_info("tmpfs mounted on /tmp");

    vfs_mkdir("/proc", 0555);
    vfs_mount("/proc", FS_TYPE_PROCFS, NULL);
    procfs_init();
    klog_info("procfs mounted on /proc");

    vfs_mkdir("/sys", 0555);
    vfs_mount("/sys", FS_TYPE_SYSFS, NULL);
    sysfs_init();
    klog_info("sysfs mounted on /sys");

    initrd_init(0, 0);
    tarfs_init();
    init_root_files();
    klog_info("VFS, initrd and tarfs initialized");

    keyboard_init();
    mouse_init();
    rtc_init();
    driver_manager_init();
    disk_manager_init();
    klog_info("Drivers and disk manager initialized");

    /* Initialize user/group management */
    user_init();
    klog_info("User management initialized");

    /* Initialize ACPI sleep/wake, battery */
    klog_info("init: battery...");
    battery_init();

    /* Initialize DRM/KMS core */
    klog_info("init: drm...");
    drm_init();

    /* PCI bus scan and driver registration */
    klog_info("init: pci bus...");
    pci_bus_init();
    klog_info("PCI bus initialized");
    pci_bus_scan();
    klog_info("PCI bus scan complete");

    /* GPU drivers (after PCI init) */
    klog_info("init: i915...");
    i915_init();
    klog_info("Intel GPU driver initialized");

    /* AMD GPU driver */
    klog_info("init: amdgpu...");
    amdgpu_init();
    klog_info("AMD GPU driver initialized");

    /* IPv6 协议栈 */
    ipv6_init();
    klog_info("IPv6 protocol stack initialized");

    /* WiFi 无线框架 */
    wifi_init();
    klog_info("WiFi framework initialized");

    /* 扩展 VFS */
    vfs_ext_init();
    klog_info("Extended VFS initialized");

    /* 注册 AIO tick hook: 每 8 个调度 tick 处理一个待处理的 AIO 请求,
     * 这样 AIO 可以在后台异步执行而不阻塞调度器 */
    {
        extern int vfs_ext_aio_process_one(void);
        extern void scheduler_set_aio_tick(aio_tick_fn_t fn);
        scheduler_set_aio_tick((aio_tick_fn_t)vfs_ext_aio_process_one);
        klog_info("AIO subsystem registered with scheduler");
    }

    /* 扩展用户系统 */
    user_ext_init();
    klog_info("Extended user management initialized");

    /* .FUN 可执行格式加载器 */
    fun_loader_init();
    klog_info(".FUN executable loader initialized");

    /* Initialize syslog service */
    syslog_init();
    klog_info("Syslog service initialized");

    /* Initialize HTTP client and package manager */
    http_client_init();
    pkgmgr_init();
    klog_info("HTTP client and package manager initialized");

    /* Read VBE info passed by bootloader from physical address 0x800.
     * Address 0x700 is inside the kernel info block (0x600-0x7FF) which
     * gets overwritten by the disk read, so bootloader stores at 0x800. */
    uint32_t vbe_magic = *(volatile uint32_t *)0x800;
    uint32_t vbe_mode_val = 0, vbe_fb_addr = 0, vbe_fb_width = 0;
    uint32_t vbe_fb_height = 0, vbe_fb_bpp = 0, vbe_fb_pitch = 0;
    int vbe_valid = 0;

    if (vbe_magic == 0xB007F1E0) {
        vbe_mode_val  = *(volatile uint32_t *)0x804;
        vbe_fb_addr   = *(volatile uint32_t *)0x808;
        vbe_fb_width  = *(volatile uint32_t *)0x80C;
        vbe_fb_height = *(volatile uint32_t *)0x810;
        vbe_fb_bpp    = *(volatile uint32_t *)0x814;
        vbe_fb_pitch  = *(volatile uint32_t *)0x818;
        klog_info("VBE raw: mode=0x%X fb=0x%X %ux%u %ubpp pitch=%u",
                  vbe_mode_val, vbe_fb_addr, vbe_fb_width, vbe_fb_height,
                  vbe_fb_bpp, vbe_fb_pitch);

        /* Validate framebuffer address from bootloader */
        if (vbe_fb_addr != 0 &&
            vbe_fb_addr >= 0xE0000000 && vbe_fb_addr < 0xFFFFFFFF &&
            (vbe_fb_addr & 0xFFF) == 0 &&
            vbe_fb_width >= 640 && vbe_fb_width <= 4096 &&
            vbe_fb_height >= 480 && vbe_fb_height <= 2160 &&
            vbe_fb_bpp >= 16 && vbe_fb_bpp <= 32) {
            vbe_valid = 1;
            klog_info("VBE: bootloader address validated");
        } else {
            /* Bootloader address is bad - try reading from raw VBE mode info
             * block at 0x0900 (where BIOS INT 10h AH=4F01h stored it).
             * Fixed: offset 0x28 is added to the ADDRESS (0x0900+0x28=0x0928),
             * not to the value read. */
            klog_info("VBE: bootloader addr invalid, trying raw VBE info at 0x0900");
            uint32_t raw_fb = *(volatile uint32_t *)(0x0900 + 0x28);
            /* Also read other fields from raw block per VBE_MODE_INFO struct */
            uint16_t raw_w = *((volatile uint16_t *)(0x0900 + 0x12));
            uint16_t raw_h = *((volatile uint16_t *)(0x0900 + 0x14));
            uint8_t  raw_b = *((volatile uint8_t  *)(0x0900 + 0x19));
            uint16_t raw_p = *((volatile uint16_t *)(0x0900 + 0x10));

            klog_info("VBE raw block: fb=0x%X %ux%u %ubpp pitch=%u",
                      raw_fb, raw_w, raw_h, raw_b, raw_p);

            if (raw_fb != 0 && raw_fb >= 0xE0000000 && raw_fb < 0xFFFFFFFF &&
                (raw_fb & 0xFFF) == 0 && raw_w >= 640 && raw_h >= 480 &&
                raw_b >= 16 && raw_b <= 32) {
                vbe_fb_addr = raw_fb;
                vbe_fb_width = raw_w;
                vbe_fb_height = raw_h;
                vbe_fb_bpp = raw_b;
                vbe_fb_pitch = raw_p;
                vbe_valid = 1;
                klog_info("VBE: raw block address validated!");
            } else {
                /* Last resort: try common QEMU Bochs VBE FB addresses.
                 * QEMU typically places LFB at 0xFD000000 or 0xE0000000.
                 * Use read-only probing to avoid corrupting MMIO devices. */
                klog_info("VBE: raw block also bad, trying known QEMU FB addresses");
                uint32_t candidates[] = { 0xFD000000, 0xE0000000, 0xF0000000 };
                for (int i = 0; i < 3; i++) {
                    /* Try to map a page at candidate address to see if it's readable */
                    vmm_map_page(vmm_get_current_dir(), candidates[i], candidates[i],
                                1); /* present only (read-only) */
                    /* Read a value - if no page fault, address is mapped */
                    volatile uint32_t *test = (volatile uint32_t *)candidates[i];
                    /* Use a safe read-only check: just try to read.
                     * If we get here without triple-faulting, the address is valid. */
                    uint32_t val = test[0];
                    (void)val;  /* suppress unused warning */
                    /* Re-map as writable now that we know it's safe */
                    vmm_map_page(vmm_get_current_dir(), candidates[i], candidates[i],
                                3); /* present+writable */
                    vbe_fb_addr = candidates[i];
                    /* Keep width/bpp/pitch from original (mode was set OK) */
                    if (vbe_fb_width < 640) vbe_fb_width = 640;
                    if (vbe_fb_height < 480) vbe_fb_height = 480;
                    if (vbe_fb_bpp < 16) vbe_fb_bpp = 24;
                    if (vbe_fb_pitch < vbe_fb_width * 3) vbe_fb_pitch = vbe_fb_width * 3;
                    vbe_valid = 1;
                    klog_info("VBE: found working FB at 0x%X", candidates[i]);
                    break;
                }
                if (!vbe_valid) {
                    klog_info("VBE: ALL address detection methods FAILED");
                }
            }
        }
    } else {
        klog_info("VBE: no bootloader info (magic=0x%X), fallback to VGA", vbe_magic);
    }

    /* Initialize VBE from (possibly corrected) info */
    if (vbe_valid) {
        vbe_init_from_multiboot(vbe_mode_val, vbe_fb_addr,
                                vbe_fb_width, vbe_fb_height,
                                vbe_fb_bpp, vbe_fb_pitch);
    }

    /* ================================================================
     * Console initialization: FORCE VGA text mode for pure CLI shell.
     * GPU drivers (drm/i915/amdgpu) may have reprogrammed the display
     * controller and broken the bootloader's VBE framebuffer setup,
     * so we explicitly switch back to VGA text mode 3 which works
     * reliably on all VGA-compatible hardware without framebuffer.
     * ================================================================ */
    {
        int console_initialized = 0;

        /* Always switch to VGA text mode 3 for CLI shell */
        klog_info("Switching to VGA text mode for command-line shell...");
        vga_text_mode3_switch();
        vga_text_init();
        vga_text_clear();

        /* Print banner */
        vga_print("  ========================================\n");
        vga_print("   FunsCore v" KERNEL_VERSION " - FunsOS CLI\n");
        vga_print("  ========================================\n\n");

        console_initialized = 1;
        serial_print(COM1, "[VGA-TEXT] Command-line shell mode ready\n");
        klog_info("Using VGA text mode console (CLI mode)");

        /* Set shell to use VGA text output */
        shell_set_vbe_mode(0);
        (void)console_initialized;
    }
    shell_init();
    klog_info("Shell initialized (available via Terminal app)");

    /* v0.9: printf-format regression smoke (tests %llu/%lld/%z and friends). */
    printf_selftest();

    /* 构建标准 Unix 风格目录结构 */
    fs_build_layout();

    /* 在启用中断前先初始化键盘 - 确保键盘中断已就绪 */
    klog_info("Verifying keyboard interrupt is ready...");
    {
        extern void pic_unmask(uint8_t irq);
        extern void ioapic_set_routing(uint8_t irq, uint8_t vector, uint8_t cpu);
        pic_unmask(1); /* 确保键盘中断(IRQ1)在PIC上未被屏蔽 */
        /* Also route through IOAPIC if present (vector 33 = IRQ 1 + 32) */
        ioapic_set_routing(1, 33, 0);
    }

    /* 启用中断 - 必须在GUI启动之前 */
    sti();
    klog_info("Interrupts enabled, keyboard ready");

    /* 初始化统一系统服务 */
    klog_info("Initializing unified system services...");
    extern int system_services_init(void);
    if (system_services_init() == 0) {
        register_core_services();
        klog_info("System services framework initialized");
    }

    /* ================================================================
     * 显示开屏动画
     * ================================================================ */
    klog_info("Initializing application registry...");
    app_registry_init();

    klog_info("Showing splash screen...");
    splash_show();

    /* ================================================================
     * 启动纯命令行 Shell 模式
     * 使用 framebuffer console (如果VBE可用) 或 VGA text mode
     * ================================================================ */
    klog_info("Starting FunsOS command-line shell...");
    shell_run();

    print_service_status();

    /* 不应到达此处 */
    klog_info("System halted");
    while (1) { hlt(); }
}
