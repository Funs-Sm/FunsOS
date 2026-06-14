/* system_services.h - 统一系统服务层
 * 深度集成SDK、OS桌面、应用和内核功能
 * 消除模块分裂，提供统一的系统服务接口
 */

#ifndef SYSTEM_SERVICES_H
#define SYSTEM_SERVICES_H

#include "stdint.h"
#include "vfs.h"

/* ========================================
 * 系统服务状态
 * ======================================== */

typedef enum {
    SYS_SERVICE_STOPPED = 0,
    SYS_SERVICE_STARTING,
    SYS_SERVICE_RUNNING,
    SYS_SERVICE_STOPPING,
    SYS_SERVICE_ERROR
} sys_service_state_t;

/* 系统服务优先级 */
typedef enum {
    SYS_PRIORITY_CRITICAL = 0,   /* 关键服务（VFS, 内存管理等） */
    SYS_PRIORITY_HIGH     = 1,   /* 高优先级（网络栈，驱动） */
    SYS_PRIORITY_NORMAL   = 2,   /* 普通服务（桌面环境，应用） */
    SYS_PRIORITY_LOW      = 3    /* 低优先级（后台任务） */
} sys_service_priority_t;

/* ========================================
 * 核心系统服务注册表
 * ======================================== */

#define MAX_SYSTEM_SERVICES 64

typedef struct system_service {
    char name[64];                           /* 服务名称 */
    char description[256];                   /* 服务描述 */
    sys_service_state_t state;               /* 服务状态 */
    sys_service_priority_t priority;         /* 优先级 */
    
    /* 服务生命周期回调 */
    int (*init)(void);                       /* 初始化 */
    int (*start)(void);                      /* 启动 */
    int (*stop)(void);                       /* 停止 */
    void (*cleanup)(void);                   /* 清理 */
    
    /* 服务依赖 */
    char dependencies[8][64];                /* 依赖的其他服务 */
    int dep_count;                           /* 依赖数量 */
    
    /* 统计信息 */
    uint64_t start_time;                     /* 启动时间 */
    uint64_t call_count;                     /* 调用次数 */
    uint32_t last_error;                     /* 最后的错误码 */
    
    void *private_data;                      /* 服务私有数据 */
} system_service_t;

/* ========================================
 * 桌面环境服务（集成os/desktop）
 * ======================================== */

typedef struct {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t color_depth;
    uint8_t desktop_active;
    uint8_t compositor_enabled;
    uint32_t window_count;
    uint32_t active_window;
} desktop_service_t;

/* 桌面环境管理 */
int sys_desktop_init(uint32_t width, uint32_t height);
int sys_desktop_start(void);
void sys_desktop_stop(void);
desktop_service_t *sys_desktop_get_state(void);

/* 窗口管理（集成window_mgr） */
typedef struct sys_window {
    uint32_t id;
    char title[128];
    int32_t x, y;
    uint32_t width, height;
    uint32_t flags;
    void *framebuffer;
    struct sys_window *next;
} sys_window_t;

sys_window_t *sys_window_create(const char *title, int32_t x, int32_t y, 
                                uint32_t width, uint32_t height, uint32_t flags);
void sys_window_destroy(sys_window_t *win);
void sys_window_set_title(sys_window_t *win, const char *title);
void sys_window_move(sys_window_t *win, int32_t x, int32_t y);
void sys_window_resize(sys_window_t *win, uint32_t width, uint32_t height);
void sys_window_show(sys_window_t *win);
void sys_window_hide(sys_window_t *win);
void sys_window_focus(sys_window_t *win);

/* 任务栏管理（集成taskbar） */
int sys_taskbar_init(void);
int sys_taskbar_add_item(const char *title, uint32_t window_id);
int sys_taskbar_remove_item(uint32_t window_id);
void sys_taskbar_update(void);

/* 开始菜单（集成start_menu） */
int sys_startmenu_init(void);
int sys_startmenu_add_entry(const char *name, const char *icon, const char *command);
void sys_startmenu_show(void);
void sys_startmenu_hide(void);

/* ========================================
 * 应用程序服务（集成os/apps）
 * ======================================== */

/* 文件管理器 */
int sys_filemanager_open(const char *path);
int sys_filemanager_create_window(void);

/* 文本编辑器 */
int sys_texteditor_open(const char *file);
int sys_texteditor_new(void);

/* 系统设置 */
int sys_settings_open(const char *category);

/* 终端 */
int sys_terminal_spawn(void);
int sys_terminal_execute(const char *command);

/* 计算器 */
int sys_calculator_open(void);
int sys_calculator_key_press(uint32_t key);
const char *sys_calculator_get_display(void);

/* 画图 */
int sys_paint_open(void);
int sys_paint_new_canvas(uint32_t width, uint32_t height);

/* ========================================
 * SDK功能集成（集成sdk/）
 * ======================================== */

/* 图形渲染服务 */
typedef struct {
    void *context;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    void *framebuffer;
} sys_graphics_context_t;

sys_graphics_context_t *sys_graphics_create_context(uint32_t width, uint32_t height);
void sys_graphics_destroy_context(sys_graphics_context_t *ctx);
void sys_graphics_clear(sys_graphics_context_t *ctx, uint32_t color);
void sys_graphics_draw_rect(sys_graphics_context_t *ctx, int32_t x, int32_t y, 
                           uint32_t w, uint32_t h, uint32_t color);
void sys_graphics_draw_text(sys_graphics_context_t *ctx, const char *text, 
                           int32_t x, int32_t y, uint32_t color);
void sys_graphics_blit(sys_graphics_context_t *dst, sys_graphics_context_t *src,
                      int32_t dst_x, int32_t dst_y, int32_t src_x, int32_t src_y,
                      uint32_t width, uint32_t height);

/* 事件系统服务 */
typedef enum {
    SYS_EVENT_NONE = 0,
    SYS_EVENT_KEY_PRESS,
    SYS_EVENT_KEY_RELEASE,
    SYS_EVENT_MOUSE_MOVE,
    SYS_EVENT_MOUSE_PRESS,
    SYS_EVENT_MOUSE_RELEASE,
    SYS_EVENT_WINDOW_CLOSE,
    SYS_EVENT_WINDOW_RESIZE,
    SYS_EVENT_TIMER,
    SYS_EVENT_NETWORK,
    SYS_EVENT_CUSTOM
} sys_event_type_t;

typedef struct sys_event {
    sys_event_type_t type;
    uint32_t timestamp;
    union {
        struct {
            uint32_t keycode;
            uint32_t modifiers;
        } key;
        struct {
            int32_t x, y;
            int32_t dx, dy;
            uint32_t buttons;
        } mouse;
        struct {
            uint32_t window_id;
            uint32_t width, height;
        } window;
        struct {
            uint32_t timer_id;
        } timer;
        struct {
            uint32_t socket_id;
            uint32_t event_mask;
        } network;
        uint8_t data[64];
    };
} sys_event_t;

int sys_event_init(void);
int sys_event_poll(sys_event_t *event);
int sys_event_wait(sys_event_t *event, uint32_t timeout_ms);
int sys_event_push(const sys_event_t *event);

/* 音频服务 */
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t buffer_size;
} sys_audio_format_t;

int sys_audio_init(void);
int sys_audio_open(sys_audio_format_t *format);
int sys_audio_play(const void *data, uint32_t size);
void sys_audio_stop(void);
void sys_audio_close(void);

/* 网络服务（集成net/） */
typedef struct sys_socket {
    uint32_t id;
    int type;  /* TCP/UDP */
    int state;
    uint32_t local_addr;
    uint16_t local_port;
    uint32_t remote_addr;
    uint16_t remote_port;
} sys_socket_t;

sys_socket_t *sys_socket_create(int type);
int sys_socket_bind(sys_socket_t *sock, uint32_t addr, uint16_t port);
int sys_socket_listen(sys_socket_t *sock, int backlog);
sys_socket_t *sys_socket_accept(sys_socket_t *sock);
int sys_socket_connect(sys_socket_t *sock, uint32_t addr, uint16_t port);
int sys_socket_send(sys_socket_t *sock, const void *data, uint32_t size);
int sys_socket_recv(sys_socket_t *sock, void *data, uint32_t size);
void sys_socket_close(sys_socket_t *sock);

/* ========================================
 * 进程和内存服务
 * ======================================== */

/* 进程管理服务 */
typedef struct sys_process {
    uint32_t pid;
    uint32_t ppid;
    char name[64];
    uint32_t state;
    uint32_t priority;
    uint64_t cpu_time;
    uint32_t memory_used;
} sys_process_t;

sys_process_t *sys_process_create(const char *name, void (*entry)(void *), void *arg);
int sys_process_kill(uint32_t pid);
int sys_process_suspend(uint32_t pid);
int sys_process_resume(uint32_t pid);
sys_process_t *sys_process_get_current(void);
int sys_process_list(sys_process_t *list, uint32_t max_count);

/* 内存管理服务 */
void *sys_mem_alloc(uint32_t size);
void *sys_mem_alloc_aligned(uint32_t size, uint32_t alignment);
void sys_mem_free(void *ptr);
void *sys_mem_realloc(void *ptr, uint32_t new_size);
int sys_mem_get_stats(uint32_t *total, uint32_t *used, uint32_t *free);

/* ========================================
 * 文件系统服务（集成VFS和高级功能）
 * ======================================== */

/* 统一文件操作接口 */
typedef struct sys_file {
    uint32_t fd;
    char path[256];
    uint32_t flags;
    uint32_t offset;
    void *private_data;
} sys_file_t;

sys_file_t *sys_file_open(const char *path, uint32_t flags);
int sys_file_close(sys_file_t *file);
int sys_file_read(sys_file_t *file, void *buffer, uint32_t size);
int sys_file_write(sys_file_t *file, const void *buffer, uint32_t size);
int sys_file_seek(sys_file_t *file, int32_t offset, int whence);
int sys_file_stat(const char *path, void *stat_buf);
int sys_file_delete(const char *path);
int sys_file_rename(const char *oldpath, const char *newpath);

/* 目录操作 */
typedef struct sys_dir {
    char path[256];
    void *handle;
} sys_dir_t;

sys_dir_t *sys_dir_open(const char *path);
int sys_dir_read(sys_dir_t *dir, char *name, uint32_t max_len);
void sys_dir_close(sys_dir_t *dir);
int sys_dir_create(const char *path);
int sys_dir_remove(const char *path);

/* ========================================
 * 系统通知和消息服务（集成os/services）
 * ======================================== */

typedef enum {
    SYS_NOTIFY_INFO = 0,
    SYS_NOTIFY_WARNING,
    SYS_NOTIFY_ERROR,
    SYS_NOTIFY_SUCCESS
} sys_notify_type_t;

int sys_notify_send(const char *title, const char *message, sys_notify_type_t type);
int sys_notify_clear(void);

/* 登录服务 */
int sys_login_authenticate(const char *username, const char *password);
int sys_login_logout(void);
int sys_login_get_current_user(char *username, uint32_t max_len);

/* ========================================
 * 系统服务管理核心
 * ======================================== */

/* 系统服务初始化 */
int system_services_init(void);

/* 注册服务 */
int sys_service_register(system_service_t *service);
int sys_service_unregister(const char *name);

/* 服务控制 */
int sys_service_start(const char *name);
int sys_service_stop(const char *name);
int sys_service_restart(const char *name);
sys_service_state_t sys_service_get_state(const char *name);

/* 服务查询 */
system_service_t *sys_service_find(const char *name);
int sys_service_list(system_service_t **list, uint32_t *count);

/* 依赖解析和启动 */
int sys_service_start_all(void);
int sys_service_stop_all(void);

/* ========================================
 * 系统信息服务
 * ======================================== */

typedef struct {
    char os_name[64];
    char kernel_version[32];
    char hostname[64];
    uint64_t uptime_seconds;
    uint32_t total_memory;
    uint32_t free_memory;
    uint32_t cached_memory;
    uint32_t cpu_count;
    uint32_t cpu_usage_percent;
    uint32_t process_count;
    uint32_t thread_count;
} sys_info_t;

int sys_info_get(sys_info_t *info);
int sys_info_get_hostname(char *hostname, uint32_t max_len);
int sys_info_set_hostname(const char *hostname);

/* ========================================
 * 定时器服务
 * ======================================== */

typedef void (*sys_timer_callback_t)(void *arg);

uint32_t sys_timer_create(uint32_t interval_ms, sys_timer_callback_t callback, void *arg);
int sys_timer_start(uint32_t timer_id);
int sys_timer_stop(uint32_t timer_id);
int sys_timer_destroy(uint32_t timer_id);

/* ========================================
 * 电源管理服务
 * ======================================== */

typedef enum {
    SYS_POWER_SHUTDOWN = 0,
    SYS_POWER_REBOOT,
    SYS_POWER_SUSPEND,
    SYS_POWER_HIBERNATE
} sys_power_action_t;

int sys_power_init(void);
int sys_power_start(void);
int sys_power_action(sys_power_action_t action);
int sys_power_action_ext(uint32_t action);
int sys_power_get_battery_status(uint32_t *percent, uint32_t *charging);
int sys_power_get_battery(int *percent, int *charging, int *remaining_minutes);
int sys_power_set_brightness(int percent);
int sys_power_get_brightness(void);
int sys_power_inhibit(const char *reason);
int sys_power_uninhibit(void);

/* ========================================
 * 剪贴板服务
 * ======================================== */

int sys_clipboard_init(void);
int sys_clipboard_set_text(const char *text);
int sys_clipboard_get_text(char *buffer, uint32_t max_len);
int sys_clipboard_clear(void);
int sys_clipboard_is_empty(void);
int sys_clipboard_has_type(uint32_t type);
int sys_clipboard_set_data(uint32_t type, const void *data, uint32_t size);
int sys_clipboard_get_data(uint32_t *type, void *buf, uint32_t *size);
int sys_clipboard_begin_drag(int x, int y, uint32_t type, const void *data, uint32_t size);
int sys_clipboard_end_drag(int *target_x, int *target_y);
int sys_clipboard_is_dragging(void);

/* ========================================
 * 系统服务更新
 * ======================================== */

void sys_services_update(void);

/* ========================================
 * 3D图形服务（集成GPU驱动）
 * ======================================== */

typedef struct sys_3d_context {
    void *hw_context;
    uint32_t width;
    uint32_t height;
} sys_3d_context_t;

sys_3d_context_t *sys_3d_create_context(uint32_t width, uint32_t height);
void sys_3d_destroy_context(sys_3d_context_t *ctx);
void sys_3d_clear(sys_3d_context_t *ctx, float r, float g, float b, float a);
void sys_3d_present(sys_3d_context_t *ctx);

/* ========================================
 * 数据库服务（集成fundb）
 * ======================================== */

typedef struct sys_db {
    char name[64];
    void *handle;
} sys_db_t;

sys_db_t *sys_db_open(const char *name);
void sys_db_close(sys_db_t *db);
int sys_db_execute(sys_db_t *db, const char *sql);
int sys_db_query(sys_db_t *db, const char *sql, void *result);

/* ========================================
 * 包管理服务
 * ======================================== */

typedef struct {
    char name[64];
    char version[32];
    char description[256];
    uint32_t size;
    uint8_t installed;
} sys_package_info_t;

int sys_package_install(const char *package_name);
int sys_package_remove(const char *package_name);
int sys_package_list(sys_package_info_t *list, uint32_t max_count);
int sys_package_update_all(void);

#endif /* SYSTEM_SERVICES_H */
