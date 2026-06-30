#ifndef FUNSOS_H
#define FUNSOS_H

#include "stdint.h"
#include "stddef.h"

/*
 * FUNSOS SDK 总头文件
 * 包含所有子模块头文件，开发者只需 #include "funsos.h" 即可使用全部 API�?
 */

/* SDK 版本 */
#define FUNSOS_SDK_VERSION_MAJOR  1
#define FUNSOS_SDK_VERSION_MINOR  3
#define FUNSOS_SDK_VERSION_PATCH  0
#define FUNSOS_SDK_VERSION "1.3.0"

/* 版本检查 - 用于编译期检查 SDK 版本兼容性 */
#define FUNSOS_VERSION_CODE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define FUNSOS_SDK_VERSION_CODE  FUNSOS_VERSION_CODE(1, 3, 0)

/* 操作系统信息（与内核 version.h 保持一致） */
#define FUNSOS_OS_NAME    "FUNSOS"
#define FUNSOS_KERNEL_NAME "FunsCore"
#define FUNSOS_KERNEL_VERSION "0.7"

/* ---- 功能特性宏 ----
 * 用于在编译时检测当�?SDK/内核支持的功能�?
 * 应用程序可通过 #ifdef 来有条件地使用高级功能�?
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
#define FUNSOS_HAS_KVM        1   /* KVM 虚拟化支�?*/
#define FUNSOS_HAS_DB         1   /* 内嵌数据库支�?*/
#define FUNSOS_HAS_FUSE       1   /* FUSE 用户态文件系�?*/
#define FUNSOS_HAS_3D_RENDER  1   /* 3D 硬件加速渲�?*/
#define funsos_render         1   /* 渲染器后端支�?*/
#define FUNSOS_HAS_SHADER     1   /* 着色器效果模拟 */
#define FUNSOS_HAS_PACKAGE    1   /* 软件包管�?*/
#define FUNSOS_HAS_CLIPBOARD  1   /* 剪贴板操�?*/
#define FUNSOS_HAS_DRAGDROP   1   /* 拖放操作 */
#define FUNSOS_HAS_HOTKEY     1   /* 全局热键注册 */
#define FUNSOS_HAS_DIALOG     1   /* 对话�?消息�?*/
#define FUNSOS_HAS_TIMER_EXT  1   /* 扩展定时�?API */
#define FUNSOS_HAS_VFS        1   /* 虚拟文件系统 */
#define FUNSOS_HAS_IPC        1   /* 进程间通信扩展 */
#define FUNSOS_HAS_NOTIFY     1   /* 系统通知 */
#define FUNSOS_HAS_SCHED      1   /* 调度器控�?*/

/* ---- 错误码枚�?----
 * 所�?FUNSOS API 函数的统一错误码定义�?
 * 正数或零表示成功，负数表示各种错误�?
 */
typedef enum {
    /* 通用错误�?*/
    FUNSOS_OK              = 0,    /* 操作成功 */
    FUNSOS_ERROR           = -1,   /* 通用错误 */
    FUNSOS_ERR_NOMEM       = -2,   /* 内存不足 */
    FUNSOS_ERR_INVAL       = -3,   /* 无效参数 */
    FUNSOS_ERR_PERM        = -4,   /* 权限不足 */
    FUNSOS_ERR_NOENT       = -5,   /* 文件/对象不存�?*/
    FUNSOS_ERR_EXIST       = -6,   /* 对象已存�?*/
    FUNSOS_ERR_BUSY        = -7,   /* 设备/资源�?*/
    FUNSOS_ERR_TIMEDOUT    = -8,   /* 操作超时 */
    FUNSOS_ERR_OVERFLOW    = -9,   /* 数值溢�?*/
    FUNSOS_ERR_UNSUPPORTED = -10,  /* 不支持的操作 */

    /* 文件系统错误�?*/
    FUNSOS_ERR_NOTDIR      = -20,  /* 不是目录 */
    FUNSOS_ERR_ISDIR       = -21,  /* 是目录（期望文件�?*/
    FUNSOS_ERR_NOSPC       = -22,  /* 设备无剩余空�?*/
    FUNSOS_ERR_BIG         = -23,  /* 文件过大 */

    /* 网络错误�?*/
    FUNSOS_ERR_NETDOWN     = -30,  /* 网络不可�?*/
    FUNSOS_ERR_CONNREFUSED = -31,  /* 连接被拒�?*/
    FUNSOS_ERR_CONNRESET   = -32,  /* 连接被重�?*/
    FUNSOS_ERR_ADDRINUSE   = -33,  /* 地址已被使用 */
    FUNSOS_ERR_NOTCONN     = -34,  /* 未连接到套接�?*/

    /* 进程错误�?*/
    FUNSOS_ERR_CHILD       = -40,  /* 子进程异�?*/
    FUNSOS_ERR_SIGNAL      = -41,  /* 信号处理错误 */

    /* 窗口/图形错误�?*/
    FUNSOS_ERR_NOCONTEXT   = -50,  /* 无图形上下文 */
    FUNSOS_ERR_BADWIN      = -51,  /* 无效窗口句柄 */

    /* 音频错误�?*/
    FUNSOS_ERR_NODEV       = -60,  /* 音频设备不存�?*/
    FUNSOS_ERR_AUDIOBUSY   = -61,  /* 音频设备�?*/
} funsos_error_t;

/* ---- 应用程序初始化配置结构体 ----
 * 传递给 funs_app_init() 用于配置应用启动参数�?
 */
typedef struct {
    const char *app_name;          /* 应用程序名称 */
    const char *version;           /* 应用版本字符�?*/
    uint32_t    flags;             /* 启动标志�?*/
    uint32_t    min_memory_kb;     /* 最小内存需�?(KB), 0=默认 */
    int         window_x;          /* 初始窗口 X 坐标, -1=自动 */
    int         window_y;          /* 初始窗口 Y 坐标, -1=自动 */
    int         window_w;          /* 初始窗口宽度, 0=默认(640) */
    int         window_h;          /* 初始窗口高度, 0=默认(480) */
} funsos_app_config_t;

/* 启动标志位常�?*/
#define FUNSOS_APP_FLAG_CONSOLE   0x01   /* 控制台模式（�?GUI�?*/
#define FUNSOS_APP_FLAG_FULLSCREEN 0x02  /* 全屏模式 */
#define FUNSOS_APP_FLAG_NOAUDIO   0x04   /* 禁用音频子系�?*/
#define FUNSOS_APP_FLAG_NONETWORK 0x08   /* 禁用网络子系�?*/
#define FUNSOS_APP_FLAG_DEBUG     0x10   /* 调试模式（输出额外日志） */

/*
 * 初始化应用程序（可选，用于带配置的启动�?
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
/* 包含所有子头文�?*/
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
#include "funsos_package.h"

int funs_get_system_info(funsos_sysinfo_t *info);

/* ---- 额外的系统调用声明 (v1.3.0 新增) ---- */

/*
 * 获取主机名
 * 参数: buf - 接收缓冲区; len - 缓冲区大小
 * 返回: 0 成功, -1 失败
 */
int funsos_gethostname(char *buf, uint32_t len);

/*
 * 设置主机名
 * 参数: name - 主机名字符串
 * 返回: 0 成功, -1 失败
 */
int funsos_sethostname(const char *name);

/*
 * 获取当前用户 ID
 * 返回: 用户 ID
 */
uint32_t funsos_getuid(void);

/*
 * 获取当前有效用户 ID
 * 返回: 有效用户 ID
 */
uint32_t funsos_geteuid(void);

/*
 * 获取当前组 ID
 * 返回: 组 ID
 */
uint32_t funsos_getgid(void);

/*
 * 获取当前有效组 ID
 * 返回: 有效组 ID
 */
uint32_t funsos_getegid(void);

/*
 * 设置用户 ID
 * 参数: uid - 用户 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_setuid(uint32_t uid);

/*
 * 设置组 ID
 * 参数: gid - 组 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_setgid(uint32_t gid);

/*
 * 获取环境变量
 * 参数: name - 环境变量名
 * 返回: 环境变量值指针, NULL 表示不存在
 */
char *funsos_getenv(const char *name);

/*
 * 设置环境变量
 * 参数: name - 变量名; value - 变量值; overwrite - 是否覆盖已存在的
 * 返回: 0 成功, -1 失败
 */
int funsos_setenv(const char *name, const char *value, int overwrite);

/*
 * 删除环境变量
 * 参数: name - 变量名
 * 返回: 0 成功, -1 失败
 */
int funsos_unsetenv(const char *name);

/*
 * 获取系统启动时间
 * 返回: 启动时的 Unix 时间戳 (秒)
 */
uint32_t funsos_get_boottime(void);

/*
 * 获取 CPU 负载平均值
 * 参数: loads - 接收 1/5/15 分钟负载的数组 (3个元素)
 * 返回: 0 成功, -1 失败
 */
int funsos_getloadavg(uint32_t *loads);

/*
 * 系统日志写入
 * 参数: priority - 日志优先级; msg - 日志消息; len - 消息长度
 * 返回: 0 成功, -1 失败
 */
int funsos_syslog(int priority, const char *msg, uint32_t len);

/*
 * 获取系统资源限制
 * 参数: resource - 资源类型; rlim - 接收限制值
 * 返回: 0 成功, -1 失败
 */
int funsos_getrlimit(int resource, uint32_t *rlim);

/*
 * 设置系统资源限制
 * 参数: resource - 资源类型; rlim - 限制值
 * 返回: 0 成功, -1 失败
 */
int funsos_setrlimit(int resource, uint32_t rlim);

/*
 * 挂载文件系统
 * 参数: source - 源设备; target - 挂载点; fstype - 文件系统类型; flags - 挂载标志
 * 返回: 0 成功, -1 失败
 */
int funsos_mount(const char *source, const char *target, const char *fstype, uint32_t flags);

/*
 * 卸载文件系统
 * 参数: target - 挂载点
 * 返回: 0 成功, -1 失败
 */
int funsos_umount(const char *target);

/*
 * 同步内存映射区域
 * 参数: addr - 起始地址; length - 长度; flags - 标志
 * 返回: 0 成功, -1 失败
 */
int funsos_msync(void *addr, uint32_t length, int flags);

/*
 * 内存锁定
 * 参数: addr - 起始地址; length - 长度
 * 返回: 0 成功, -1 失败
 */
int funsos_mlock(const void *addr, uint32_t length);

/*
 * 内存解锁
 * 参数: addr - 起始地址; length - 长度
 * 返回: 0 成功, -1 失败
 */
int funsos_munlock(const void *addr, uint32_t length);

/*
 * 全部内存锁定
 * 返回: 0 成功, -1 失败
 */
int funsos_mlockall(void);

/*
 * 全部内存解锁
 * 返回: 0 成功, -1 失败
 */
int funsos_munlockall(void);

/*
 * 获取线程 ID
 * 返回: 当前线程 ID
 */
uint32_t funsos_gettid(void);

/*
 * 线程设置名称
 * 参数: name - 线程名称
 * 返回: 0 成功, -1 失败
 */
int funsos_set_thread_name(const char *name);

/*
 * 线程获取名称
 * 参数: buf - 接收缓冲区; len - 缓冲区大小
 * 返回: 0 成功, -1 失败
 */
int funsos_get_thread_name(char *buf, uint32_t len);

/*
 * 信号量初始化
 * 参数: sem - 信号量指针; pshared - 进程间共享; value - 初始值
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_init(void *sem, int pshared, uint32_t value);

/*
 * 信号量销毁
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_destroy(void *sem);

/*
 * 信号量等待 (P操作)
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_wait(void *sem);

/*
 * 信号量尝试等待
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_trywait(void *sem);

/*
 * 信号量发布 (V操作)
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_post(void *sem);

/*
 * 共享内存创建/打开
 * 参数: key - 键值; size - 大小; flags - 标志
 * 返回: 共享内存 ID, -1 失败
 */
int funsos_shmget(uint32_t key, uint32_t size, int flags);

/*
 * 共享内存附加
 * 参数: shmid - 共享内存 ID; shmaddr - 目标地址 (NULL=自动)
 * 返回: 附加后的地址指针, NULL 失败
 */
void *funsos_shmat(int shmid, const void *shmaddr);

/*
 * 共享内存分离
 * 参数: shmaddr - 共享内存地址
 * 返回: 0 成功, -1 失败
 */
int funsos_shmdt(const void *shmaddr);

/*
 * 共享内存控制
 * 参数: shmid - 共享内存 ID; cmd - 命令; buf - 缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_shmctl(int shmid, int cmd, void *buf);

/*
 * 消息队列创建/打开
 * 参数: key - 键值; msgflg - 标志
 * 返回: 消息队列 ID, -1 失败
 */
int funsos_msgget(uint32_t key, int msgflg);

/*
 * 发送消息
 * 参数: msqid - 消息队列 ID; msgp - 消息指针; msgsz - 消息大小; msgflg - 标志
 * 返回: 0 成功, -1 失败
 */
int funsos_msgsnd(int msqid, const void *msgp, uint32_t msgsz, int msgflg);

/*
 * 接收消息
 * 参数: msqid - 消息队列 ID; msgp - 接收缓冲区; msgsz - 缓冲区大小; msgtyp - 消息类型; msgflg - 标志
 * 返回: 实际读取的字节数, -1 失败
 */
int funsos_msgrcv(int msqid, void *msgp, uint32_t msgsz, long msgtyp, int msgflg);

/*
 * 消息队列控制
 * 参数: msqid - 消息队列 ID; cmd - 命令; buf - 缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_msgctl(int msqid, int cmd, void *buf);

/*
 * 获取网络接口信息
 * 参数: buf - 接收缓冲区; len - 缓冲区大小
 * 返回: 接口数量, -1 失败
 */
int funsos_net_ifconf(void *buf, uint32_t len);

/*
 * 获取路由表信息
 * 参数: buf - 接收缓冲区; len - 缓冲区大小
 * 返回: 路由数量, -1 失败
 */
int funsos_net_rtconf(void *buf, uint32_t len);

/*
 * DNS 域名解析
 * 参数: hostname - 主机名; addr - 接收解析后的 IP 地址
 * 返回: 0 成功, -1 失败
 */
int funsos_dns_resolve(const char *hostname, funsos_ipv4_t *addr);

/*
 * 反向 DNS 解析
 * 参数: addr - IP 地址; buf - 接收主机名的缓冲区; len - 缓冲区大小
 * 返回: 0 成功, -1 失败
 */
int funsos_dns_reverse(funsos_ipv4_t addr, char *buf, uint32_t len);

/*
 * 获取文件系统统计信息
 * 参数: path - 路径; buf - 接收统计信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_statfs(const char *path, void *buf);

/*
 * 改变文件权限
 * 参数: path - 文件路径; mode - 权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_chmod(const char *path, uint32_t mode);

/*
 * 改变文件所有者
 * 参数: path - 文件路径; owner - 所有者 ID; group - 组 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_chown(const char *path, uint32_t owner, uint32_t group);

/*
 * 创建硬链接
 * 参数: oldpath - 原路径; newpath - 新路径
 * 返回: 0 成功, -1 失败
 */
int funsos_link(const char *oldpath, const char *newpath);

/*
 * 设备控制 (ioctl)
 * 参数: fd - 文件描述符; request - 请求码; arg - 参数
 * 返回: 0 成功, -1 失败
 */
int funsos_ioctl(int fd, uint32_t request, uint32_t arg);

/*
 * 获取终端属性
 * 参数: fd - 文件描述符; termios - 接收属性的结构体
 * 返回: 0 成功, -1 失败
 */
int funsos_tcgetattr(int fd, void *termios);

/*
 * 设置终端属性
 * 参数: fd - 文件描述符; optional_actions - 操作类型; termios - 属性结构体
 * 返回: 0 成功, -1 失败
 */
int funsos_tcsetattr(int fd, int optional_actions, const void *termios);

#endif /* FUNSOS_H */
