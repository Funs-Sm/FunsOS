#ifndef FUNSOS_H
#define FUNSOS_H

#include "stdint.h"
#include "stddef.h"

/*
 * FUNSOS SDK 总头文件
 * 包含所有子模块头文件，开发者只需 #include "funsos.h" 即可使用全部 API。
 */

/* SDK 版本号 */
#define FUNSOS_SDK_VERSION_MAJOR  1
#define FUNSOS_SDK_VERSION_MINOR  0
#define FUNSOS_SDK_VERSION_PATCH  0
#define FUNSOS_SDK_VERSION "1.0.0"

/* 版本匹配宏 - 用于编译期检查 SDK 版本兼容性 */
#define FUNSOS_VERSION_CODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define FUNSOS_SDK_VERSION_CODE  FUNSOS_VERSION_CODE(1, 0, 0)

/* 操作系统信息（与内核 version.h 保持一致） */
#define FUNSOS_OS_NAME    "FUNSOS"
#define FUNSOS_KERNEL_NAME "FunsCore"
#define FUNSOS_KERNEL_VERSION "0.5"

/* ---- 功能特性宏 ----
 * 用于在编译时检测当前 SDK/内核支持的功能。
 * 应用程序可通过 #ifdef 来有条件地使用高级功能。
 */

/* 核心功能（始终可用） */
#define FUNSOS_HAS_WINDOW     1   /* 窗口管理 */
#define FUNSOS_HAS_GRAPHICS   1   /* 2D/3D 图形绘制 */
#define FUNSOS_HAS_EVENTS     1   /* 事件系统 */
#define FUNSOS_HAS_FILES      1   /* 文件系统操作 */
#define FUNSOS_HAS_PROCESS    1   /* 进程管理 */
#define FUNSOS_HAS_MEMORY     1   /* 内存管理 */
#define FUNSOS_HAS_NETWORK    1   /* 网络通信 (TCP/UDP) */
#define FUNSOS_HAS_AUDIO      1   /* 音频播放 */
#define FUNSOS_HAS_SYSINFO    1   /* 系统信息查询 */

/* 扩展功能（需要对应内核模块支持） */
#define FUNSOS_HAS_KVM        1   /* KVM 虚拟化支持 */
#define FUNSOS_HAS_DB         1   /* 内嵌数据库支持 */
#define FUNSOS_HAS_FUSE       1   /* FUSE 用户态文件系统 */
#define FUNSOS_HAS_3D_RENDER  1   /* 3D 硬件加速渲染 */
#define funsos_render         1   /* 渲染器后端支持 */
#define FUNSOS_HAS_SHADER     1   /* 着色器效果模拟 */
#define FUNSOS_HAS_PACKAGE    1   /* 软件包管理 */
#define FUNSOS_HAS_CLIPBOARD  1   /* 剪贴板操作 */
#define FUNSOS_HAS_DRAGDROP   1   /* 拖放操作 */
#define FUNSOS_HAS_HOTKEY     1   /* 全局热键注册 */
#define FUNSOS_HAS_DIALOG     1   /* 对话框/消息框 */
#define FUNSOS_HAS_TIMER_EXT  1   /* 扩展定时器 API */
#define FUNSOS_HAS_VFS        1   /* 虚拟文件系统 */
#define FUNSOS_HAS_IPC        1   /* 进程间通信扩展 */

/* ---- 错误码枚举 ----
 * 所有 FUNSOS API 函数的统一错误码定义。
 * 正数或零表示成功，负数表示各种错误。
 */
typedef enum {
    /* 通用错误码 */
    FUNSOS_OK              = 0,    /* 操作成功 */
    FUNSOS_ERROR           = -1,   /* 通用错误 */
    FUNSOS_ERR_NOMEM       = -2,   /* 内存不足 */
    FUNSOS_ERR_INVAL       = -3,   /* 无效参数 */
    FUNSOS_ERR_PERM        = -4,   /* 权限不足 */
    FUNSOS_ERR_NOENT       = -5,   /* 文件/对象不存在 */
    FUNSOS_ERR_EXIST       = -6,   /* 对象已存在 */
    FUNSOS_ERR_BUSY        = -7,   /* 设备/资源忙 */
    FUNSOS_ERR_TIMEDOUT    = -8,   /* 操作超时 */
    FUNSOS_ERR_OVERFLOW    = -9,   /* 数值溢出 */
    FUNSOS_ERR_UNSUPPORTED = -10,  /* 不支持的操作 */

    /* 文件系统错误码 */
    FUNSOS_ERR_NOTDIR      = -20,  /* 不是目录 */
    FUNSOS_ERR_ISDIR       = -21,  /* 是目录（期望文件） */
    FUNSOS_ERR_NOSPC       = -22,  /* 设备无剩余空间 */
    FUNSOS_ERR_BIG         = -23,  /* 文件过大 */

    /* 网络错误码 */
    FUNSOS_ERR_NETDOWN     = -30,  /* 网络不可达 */
    FUNSOS_ERR_CONNREFUSED = -31,  /* 连接被拒绝 */
    FUNSOS_ERR_CONNRESET   = -32,  /* 连接被重置 */
    FUNSOS_ERR_ADDRINUSE   = -33,  /* 地址已被使用 */
    FUNSOS_ERR_NOTCONN     = -34,  /* 未连接到套接字 */

    /* 进程错误码 */
    FUNSOS_ERR_CHILD       = -40,  /* 子进程异常 */
    FUNSOS_ERR_SIGNAL      = -41,  /* 信号处理错误 */

    /* 窗口/图形错误码 */
    FUNSOS_ERR_NOCONTEXT   = -50,  /* 无图形上下文 */
    FUNSOS_ERR_BADWIN      = -51,  /* 无效窗口句柄 */

    /* 音频错误码 */
    FUNSOS_ERR_NODEV       = -60,  /* 音频设备不存在 */
    FUNSOS_ERR_AUDIOBUSY   = -61,  /* 音频设备忙 */
} funsos_error_t;

/* ---- 应用程序初始化配置结构体 ----
 * 传递给 funs_app_init() 用于配置应用启动参数。
 */
typedef struct {
    const char *app_name;          /* 应用程序名称 */
    const char *version;           /* 应用版本字符串 */
    uint32_t    flags;             /* 启动标志位 */
    uint32_t    min_memory_kb;     /* 最小内存需求 (KB), 0=默认 */
    int         window_x;          /* 初始窗口 X 坐标, -1=自动 */
    int         window_y;          /* 初始窗口 Y 坐标, -1=自动 */
    int         window_w;          /* 初始窗口宽度, 0=默认(640) */
    int         window_h;          /* 初始窗口高度, 0=默认(480) */
} funsos_app_config_t;

/* 启动标志位常量 */
#define FUNSOS_APP_FLAG_CONSOLE   0x01   /* 控制台模式（无 GUI） */
#define FUNSOS_APP_FLAG_FULLSCREEN 0x02  /* 全屏模式 */
#define FUNSOS_APP_FLAG_NOAUDIO   0x04   /* 禁用音频子系统 */
#define FUNSOS_APP_FLAG_NONETWORK 0x08   /* 禁用网络子系统 */
#define FUNSOS_APP_FLAG_DEBUG     0x10   /* 调试模式（输出额外日志） */

/*
 * 初始化应用程序（可选，用于带配置的启动）
 * 参数: config - 应用配置结构体指针，NULL 使用默认配置
 * 返回: FUNSOS_OK 成功, 其他值见 funsos_error_t
 */
int funs_app_init(const funsos_app_config_t *config);

/*
 * 反初始化应用程序（清理资源）
 * 返回: FUNSOS_OK 成功
 */
int funs_app_cleanup(void);

/*
 * 查询系统综合信息（便捷封装）
 * 参数: info - 接收信息的结构体指针
 * 返回: FUNSOS_OK 成功, 错误码见 funsos_error_t
 */
/* 包含所有子头文件 */
#include "funsos_event.h"
#include "funsos_window.h"
#include "funsos_audio.h"
#include "funsos_graphics.h"
#include "funsos_event.h"
#include "funsos_files.h"
#include "funsos_process.h"
#include "funsos_memory.h"
#include "funsos_network.h"
#include "funsos_sysinfo.h"
#include "funsos_power.h"
#include "funsos_clipboard.h"
#include "funsos_database.h"
#include "funsos_time.h"
#include "funsos_ipc.h"
#include "funsos_libc.h"

int funs_get_system_info(funsos_sysinfo_t *info);

#endif /* FUNSOS_H */
