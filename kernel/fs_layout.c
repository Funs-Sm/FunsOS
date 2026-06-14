#include "fs_layout.h"
#include "vfs.h"
#include "klog.h"

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
}
