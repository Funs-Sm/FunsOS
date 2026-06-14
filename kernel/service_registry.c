/* service_registry.c - 系统服务注册
 * 将所有系统模块注册为统一服务
 */

#include "system_services.h"
#include "klog.h"
#include "string.h"

/* VFS服务 */
static int vfs_service_init(void) {
    extern void vfs_init(void);
    vfs_init();
    return 0;
}

static int vfs_service_start(void) {
    /* VFS已在init中启动 */
    return 0;
}

static system_service_t vfs_service = {
    .name = "vfs",
    .description = "Virtual File System - 统一文件系统接口",
    .priority = SYS_PRIORITY_CRITICAL,
    .init = vfs_service_init,
    .start = vfs_service_start,
    .dep_count = 0
};

/* 桌面环境服务 */
static int desktop_service_init(void) {
    extern int is_vbe_mode(void);
    if (is_vbe_mode()) {
        /* 从VBE获取分辨率 */
        return sys_desktop_init(1024, 768); /* 默认分辨率 */
    }
    return sys_desktop_init(80, 25); /* VGA文本模式 */
}

static int desktop_service_start(void) {
    int ret = sys_desktop_start();
    if (ret == 0) {
        klog_info("Desktop environment started");
    }
    return ret;
}

static system_service_t desktop_service = {
    .name = "desktop",
    .description = "Desktop Environment - 集成窗口管理、任务栏、开始菜单",
    .priority = SYS_PRIORITY_NORMAL,
    .init = desktop_service_init,
    .start = desktop_service_start,
    .dependencies = {"vfs", "events"},
    .dep_count = 2
};

/* 事件系统服务 */
static int event_service_init(void) {
    return sys_event_init();
}

static int event_service_start(void) {
    klog_info("Event system started");
    return 0;
}

static system_service_t event_service = {
    .name = "events",
    .description = "Event System - 统一事件分发（键盘、鼠标、窗口等）",
    .priority = SYS_PRIORITY_HIGH,
    .init = event_service_init,
    .start = event_service_start,
    .dep_count = 0
};

/* 网络服务 */
static int network_service_init(void) {
    extern void net_init(void);
    net_init();
    return 0;
}

static int network_service_start(void) {
    klog_info("Network stack started");
    return 0;
}

static system_service_t network_service = {
    .name = "network",
    .description = "Network Stack - TCP/IP网络协议栈",
    .priority = SYS_PRIORITY_HIGH,
    .init = network_service_init,
    .start = network_service_start,
    .dep_count = 0
};

/* 音频服务 */
static int audio_service_init(void) {
    return sys_audio_init();
}

static int audio_service_start(void) {
    klog_info("Audio system started");
    return 0;
}

static system_service_t audio_service = {
    .name = "audio",
    .description = "Audio System - 音频播放服务",
    .priority = SYS_PRIORITY_NORMAL,
    .init = audio_service_init,
    .start = audio_service_start,
    .dep_count = 0
};

/* 任务栏服务 */
static int taskbar_service_init(void) {
    return sys_taskbar_init();
}

static int taskbar_service_start(void) {
    klog_info("Taskbar started");
    return 0;
}

static system_service_t taskbar_service = {
    .name = "taskbar",
    .description = "Taskbar - 系统任务栏",
    .priority = SYS_PRIORITY_NORMAL,
    .init = taskbar_service_init,
    .start = taskbar_service_start,
    .dependencies = {"desktop"},
    .dep_count = 1
};

/* 开始菜单服务 */
static int startmenu_service_init(void) {
    return sys_startmenu_init();
}

static int startmenu_service_start(void) {
    klog_info("Start menu started");
    return 0;
}

static system_service_t startmenu_service = {
    .name = "startmenu",
    .description = "Start Menu - 应用启动菜单",
    .priority = SYS_PRIORITY_NORMAL,
    .init = startmenu_service_init,
    .start = startmenu_service_start,
    .dependencies = {"desktop"},
    .dep_count = 1
};

/* 登录服务 */
static int login_service_init(void) {
    klog_info("Login service initialized");
    return 0;
}

static int login_service_start(void) {
    klog_info("Login service ready");
    return 0;
}

static system_service_t login_service = {
    .name = "login",
    .description = "Login Service - 用户认证服务",
    .priority = SYS_PRIORITY_HIGH,
    .init = login_service_init,
    .start = login_service_start,
    .dependencies = {"vfs"},
    .dep_count = 1
};

/* 通知服务 */
static int notification_service_init(void) {
    klog_info("Notification service initialized");
    return 0;
}

static int notification_service_start(void) {
    klog_info("Notification service started");
    return 0;
}

static system_service_t notification_service = {
    .name = "notification",
    .description = "Notification Service - 系统通知服务",
    .priority = SYS_PRIORITY_NORMAL,
    .init = notification_service_init,
    .start = notification_service_start,
    .dependencies = {"desktop"},
    .dep_count = 1
};

/* 文件管理器服务 */
static int filemanager_service_init(void) {
    klog_info("File manager initialized");
    return 0;
}

static system_service_t filemanager_service = {
    .name = "filemanager",
    .description = "File Manager - 图形化文件管理器",
    .priority = SYS_PRIORITY_LOW,
    .init = filemanager_service_init,
    .dependencies = {"desktop", "vfs"},
    .dep_count = 2
};

/* 终端服务 */
static int terminal_service_init(void) {
    klog_info("Terminal service initialized");
    return 0;
}

static system_service_t terminal_service = {
    .name = "terminal",
    .description = "Terminal - 命令行终端",
    .priority = SYS_PRIORITY_LOW,
    .init = terminal_service_init,
    .dependencies = {"desktop"},
    .dep_count = 1
};

/* 注册所有核心服务 */
int register_core_services(void) {
    klog_info("Registering core system services...");
    
    /* 按优先级顺序注册 */
    sys_service_register(&vfs_service);
    sys_service_register(&event_service);
    sys_service_register(&network_service);
    sys_service_register(&login_service);
    sys_service_register(&audio_service);
    sys_service_register(&desktop_service);
    sys_service_register(&taskbar_service);
    sys_service_register(&startmenu_service);
    sys_service_register(&notification_service);
    sys_service_register(&filemanager_service);
    sys_service_register(&terminal_service);
    
    klog_info("Registered %d system services", 11);
    return 0;
}

/* 获取服务统计信息 */
void print_service_status(void) {
    klog_info("=== System Services Status ===");
    
    /* 直接访问服务查找API */
    const char *service_names[] = {
        "vfs", "events", "network", "login", "audio",
        "desktop", "taskbar", "startmenu", "notification",
        "filemanager", "terminal"
    };
    
    for (uint32_t i = 0; i < 11; i++) {
        system_service_t *svc = sys_service_find(service_names[i]);
        if (!svc) continue;
        
        const char *state_str = "UNKNOWN";
        switch (svc->state) {
            case SYS_SERVICE_STOPPED: state_str = "STOPPED"; break;
            case SYS_SERVICE_STARTING: state_str = "STARTING"; break;
            case SYS_SERVICE_RUNNING: state_str = "RUNNING"; break;
            case SYS_SERVICE_STOPPING: state_str = "STOPPING"; break;
            case SYS_SERVICE_ERROR: state_str = "ERROR"; break;
        }
        klog_info("  [%s] %s - %s", state_str, svc->name, svc->description);
    }
}
