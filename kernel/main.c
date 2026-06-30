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
#include "ramfs.h"
#include "devfs.h"
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

#include "vga_text.h"
#include "serial.h"
#include "service_registry.h"
#include "splash.h"
#include "app_registry.h"

static inline void sti(void) {
    asm volatile("sti");
}

static inline void hlt(void) {
    asm volatile("hlt");
}

void kernel_main(void) {
    /* Initialize serial port FIRST - before any klog output.
     * Without this, serial_putchar() polls LSR bit 5 which may
     * never be set on an uninitialised UART, causing klog to hang. */
    serial_init(COM1);
    serial_print(COM1, "[early] serial COM1 initialized\n");

    init_gdt();
    /* Set up TSS kernel stack for interrupt delivery */
    {
        extern void gdt_set_tss(uint32_t ss0, uint32_t esp0);
        uint32_t kern_esp;
        asm volatile("mov %%esp, %0" : "=r"(kern_esp));
        gdt_set_tss(0x10, kern_esp);
    }
    init_idt();
    init_irq();
    fpu_init();
    init_timer();

    /* Initialize kernel log ring buffer early */
    klog_init();
    klog_info("Kernel log initialized");

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
    devfs_init();
    initrd_init(0, 0);
    tarfs_init();
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

    /* Initialize ACPI sleep/wake, CPU frequency scaling, battery */
    cpufreq_init();
    battery_init();

    /* Initialize DRM/KMS and GPU drivers */
    drm_init();
    i915_init();


    /* PCI 总线扫描和驱动注册 */
    pci_bus_init();
    klog_info("PCI bus initialized");
    pci_bus_scan();
    klog_info("PCI bus scan complete");

    /* AMD GPU 驱动 */
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
