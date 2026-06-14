#include "system_services.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "klog.h"
#include "vfs_advanced.h"

/* OS 模块集成 - 深度集成桌面环境、通知、应用等服务 */
#include "desktop.h"
#include "taskbar.h"
#include "start_menu.h"
#include "window_mgr.h"
#include "notification.h"
#include "login_service.h"
#include "text_editor.h"
#include "terminal.h"
#include "settings.h"
#include "file_manager.h"
#include "calculator.h"
#include "paint.h"
#include "power_service.h"
#include "clipboard_service.h"

/* 全局服务注册表 */
static system_service_t *service_registry[MAX_SYSTEM_SERVICES];
static uint32_t service_count = 0;
static spinlock_t service_lock;

/* ==================== 系统服务管理核心 ==================== */

int system_services_init(void) {
    spinlock_init(&service_lock);
    memset(service_registry, 0, sizeof(service_registry));
    service_count = 0;
    
    klog_info("System Services Framework initialized");
    return 0;
}

int sys_service_register(system_service_t *service) {
    if (!service || !service->name[0]) return -1;
    
    spinlock_lock(&service_lock);
    
    if (service_count >= MAX_SYSTEM_SERVICES) {
        spinlock_unlock(&service_lock);
        return -1;
    }
    
    /* 检查重复 */
    for (uint32_t i = 0; i < service_count; i++) {
        if (strcmp(service_registry[i]->name, service->name) == 0) {
            spinlock_unlock(&service_lock);
            return -1;
        }
    }
    
    service_registry[service_count++] = service;
    service->state = SYS_SERVICE_STOPPED;
    
    spinlock_unlock(&service_lock);
    klog_info("Service registered: %s", service->name);
    return 0;
}

system_service_t *sys_service_find(const char *name) {
    if (!name) return NULL;
    
    spinlock_lock(&service_lock);
    for (uint32_t i = 0; i < service_count; i++) {
        if (strcmp(service_registry[i]->name, name) == 0) {
            system_service_t *svc = service_registry[i];
            spinlock_unlock(&service_lock);
            return svc;
        }
    }
    spinlock_unlock(&service_lock);
    return NULL;
}

static int sys_service_start_with_deps(system_service_t *service) {
    if (!service) return -1;
    if (service->state == SYS_SERVICE_RUNNING) return 0;
    
    /* 先启动依赖服务 */
    for (int i = 0; i < service->dep_count; i++) {
        system_service_t *dep = sys_service_find(service->dependencies[i]);
        if (!dep) {
            klog_err("Dependency not found: %s", service->dependencies[i]);
            return -1;
        }
        if (sys_service_start_with_deps(dep) != 0) {
            return -1;
        }
    }
    
    /* 初始化服务 */
    service->state = SYS_SERVICE_STARTING;
    if (service->init && service->init() != 0) {
        service->state = SYS_SERVICE_ERROR;
        klog_err("Service init failed: %s", service->name);
        return -1;
    }
    
    /* 启动服务 */
    if (service->start && service->start() != 0) {
        service->state = SYS_SERVICE_ERROR;
        klog_err("Service start failed: %s", service->name);
        return -1;
    }
    
    service->state = SYS_SERVICE_RUNNING;
    extern uint64_t timer_get_ticks(void);
    service->start_time = timer_get_ticks();
    klog_info("Service started: %s", service->name);
    return 0;
}

int sys_service_start(const char *name) {
    system_service_t *service = sys_service_find(name);
    if (!service) return -1;
    return sys_service_start_with_deps(service);
}

int sys_service_stop(const char *name) {
    system_service_t *service = sys_service_find(name);
    if (!service) return -1;
    if (service->state != SYS_SERVICE_RUNNING) return 0;
    
    service->state = SYS_SERVICE_STOPPING;
    if (service->stop) {
        service->stop();
    }
    service->state = SYS_SERVICE_STOPPED;
    klog_info("Service stopped: %s", service->name);
    return 0;
}

int sys_service_start_all(void) {
    klog_info("Starting all system services...");
    
    /* 按优先级排序启动 */
    for (int priority = SYS_PRIORITY_CRITICAL; priority <= SYS_PRIORITY_LOW; priority++) {
        for (uint32_t i = 0; i < service_count; i++) {
            if ((int)service_registry[i]->priority == priority) {
                sys_service_start_with_deps(service_registry[i]);
            }
        }
    }
    
    klog_info("All services started");
    return 0;
}

int sys_service_stop_all(void) {
    /* 反向优先级停止 */
    for (int priority = SYS_PRIORITY_LOW; priority >= SYS_PRIORITY_CRITICAL; priority--) {
        for (uint32_t i = 0; i < service_count; i++) {
            if ((int)service_registry[i]->priority == priority) {
                sys_service_stop(service_registry[i]->name);
            }
        }
    }
    return 0;
}

/* ==================== 桌面环境服务实现 ==================== */

static desktop_service_t desktop_state = {0};
static sys_window_t *window_list = NULL;
static uint32_t next_window_id = 1;
static spinlock_t window_lock;

int sys_desktop_init(uint32_t width, uint32_t height) {
    spinlock_init(&window_lock);
    desktop_state.screen_width = width;
    desktop_state.screen_height = height;
    desktop_state.color_depth = 32;
    desktop_state.desktop_active = 0;
    desktop_state.compositor_enabled = 1;
    desktop_state.window_count = 0;
    return 0;
}

int sys_desktop_start(void) {
    desktop_state.desktop_active = 1;
    return 0;
}

void sys_desktop_stop(void) {
    desktop_state.desktop_active = 0;
}

desktop_service_t *sys_desktop_get_state(void) {
    return &desktop_state;
}

sys_window_t *sys_window_create(const char *title, int32_t x, int32_t y,
                                uint32_t width, uint32_t height, uint32_t flags) {
    sys_window_t *win = (sys_window_t *)kmalloc(sizeof(sys_window_t));
    if (!win) return NULL;
    
    memset(win, 0, sizeof(sys_window_t));
    win->id = next_window_id++;
    strncpy(win->title, title, 127);
    win->title[127] = '\0';
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    
    /* 分配framebuffer */
    win->framebuffer = kmalloc(width * height * 4);
    if (!win->framebuffer) {
        kfree(win);
        return NULL;
    }
    
    spinlock_lock(&window_lock);
    win->next = window_list;
    window_list = win;
    desktop_state.window_count++;
    spinlock_unlock(&window_lock);
    
    return win;
}

void sys_window_destroy(sys_window_t *win) {
    if (!win) return;
    
    spinlock_lock(&window_lock);
    sys_window_t *prev = NULL;
    sys_window_t *curr = window_list;
    
    while (curr) {
        if (curr == win) {
            if (prev) {
                prev->next = curr->next;
            } else {
                window_list = curr->next;
            }
            if (win->framebuffer) kfree(win->framebuffer);
            kfree(win);
            desktop_state.window_count--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    spinlock_unlock(&window_lock);
}

void sys_window_set_title(sys_window_t *win, const char *title) {
    if (!win || !title) return;
    strncpy(win->title, title, 127);
    win->title[127] = '\0';
}

void sys_window_move(sys_window_t *win, int32_t x, int32_t y) {
    if (!win) return;
    win->x = x;
    win->y = y;
}

void sys_window_resize(sys_window_t *win, uint32_t width, uint32_t height) {
    if (!win) return;
    
    /* 重新分配framebuffer */
    if (win->framebuffer) kfree(win->framebuffer);
    win->framebuffer = kmalloc(width * height * 4);
    if (!win->framebuffer) return;
    
    win->width = width;
    win->height = height;
}

void sys_window_show(sys_window_t *win) {
    if (!win) return;
    win->flags |= 0x0001; /* VISIBLE flag */
}

void sys_window_hide(sys_window_t *win) {
    if (!win) return;
    win->flags &= ~0x0001;
}

void sys_window_focus(sys_window_t *win) {
    if (!win) return;
    desktop_state.active_window = win->id;
}

/* ==================== 事件系统实现 ==================== */

#define MAX_EVENTS 256
static sys_event_t event_queue[MAX_EVENTS];
static uint32_t event_head = 0;
static uint32_t event_tail = 0;
static spinlock_t event_lock;

int sys_event_init(void) {
    spinlock_init(&event_lock);
    event_head = event_tail = 0;
    return 0;
}

int sys_event_push(const sys_event_t *event) {
    if (!event) return -1;
    
    spinlock_lock(&event_lock);
    uint32_t next_tail = (event_tail + 1) % MAX_EVENTS;
    if (next_tail == event_head) {
        spinlock_unlock(&event_lock);
        return -1; /* 队列满 */
    }
    
    event_queue[event_tail] = *event;
    event_tail = next_tail;
    spinlock_unlock(&event_lock);
    return 0;
}

int sys_event_poll(sys_event_t *event) {
    if (!event) return -1;
    
    spinlock_lock(&event_lock);
    if (event_head == event_tail) {
        spinlock_unlock(&event_lock);
        return -1; /* 队列空 */
    }
    
    *event = event_queue[event_head];
    event_head = (event_head + 1) % MAX_EVENTS;
    spinlock_unlock(&event_lock);
    return 0;
}

int sys_event_wait(sys_event_t *event, uint32_t timeout_ms) {
    /* TODO: 实现超时等待 */
    (void)timeout_ms;
    return sys_event_poll(event);
}

/* ==================== 内存管理服务 ==================== */

void *sys_mem_alloc(uint32_t size) {
    return kmalloc(size);
}

void *sys_mem_alloc_aligned(uint32_t size, uint32_t alignment) {
    /* TODO: 实现对齐分配 */
    (void)alignment;
    return kmalloc(size);
}

void sys_mem_free(void *ptr) {
    kfree(ptr);
}

void *sys_mem_realloc(void *ptr, uint32_t new_size) {
    if (!ptr) return kmalloc(new_size);
    void *new_ptr = kmalloc(new_size);
    if (new_ptr && ptr) {
        /* 复制旧数据 - 使用 memcpy 从 kernel 已集成 string.h */
        memcpy(new_ptr, ptr, new_size < 4096 ? new_size : 4096);
        kfree(ptr);
    }
    return new_ptr;
}

int sys_mem_get_stats(uint32_t *total, uint32_t *used, uint32_t *free) {
    /* 简化实现 - 返回估计值 */
    if (total) *total = 128 * 1024 * 1024; /* 128MB */
    if (used) *used = 32 * 1024 * 1024;    /* 32MB */
    if (free) *free = 96 * 1024 * 1024;    /* 96MB */
    return 0;
}

/* ==================== 文件系统服务 ==================== */

/* 注意：sys_file_* 函数已经在sys_api.c中定义，这里使用它们 */
extern sys_file_t *sys_file_open(const char *path, uint32_t flags);
extern int sys_file_close(sys_file_t *file);
extern int sys_file_read(sys_file_t *file, void *buffer, uint32_t size);
extern int sys_file_write(sys_file_t *file, const void *buffer, uint32_t size);

int sys_file_seek(sys_file_t *file, int32_t offset, int whence) {
    if (!file || !file->private_data) return -1;
    return vfs_seek((file_t *)file->private_data, offset, whence);
}

/* ==================== 图形渲染服务 ==================== */

sys_graphics_context_t *sys_graphics_create_context(uint32_t width, uint32_t height) {
    sys_graphics_context_t *ctx = (sys_graphics_context_t *)kmalloc(sizeof(sys_graphics_context_t));
    if (!ctx) return NULL;
    
    ctx->width = width;
    ctx->height = height;
    ctx->bpp = 32;
    ctx->framebuffer = kmalloc(width * height * 4);
    if (!ctx->framebuffer) {
        kfree(ctx);
        return NULL;
    }
    
    memset(ctx->framebuffer, 0, width * height * 4);
    return ctx;
}

void sys_graphics_destroy_context(sys_graphics_context_t *ctx) {
    if (!ctx) return;
    if (ctx->framebuffer) kfree(ctx->framebuffer);
    kfree(ctx);
}

void sys_graphics_clear(sys_graphics_context_t *ctx, uint32_t color) {
    if (!ctx || !ctx->framebuffer) return;
    uint32_t *fb = (uint32_t *)ctx->framebuffer;
    for (uint32_t i = 0; i < ctx->width * ctx->height; i++) {
        fb[i] = color;
    }
}

void sys_graphics_draw_rect(sys_graphics_context_t *ctx, int32_t x, int32_t y,
                           uint32_t w, uint32_t h, uint32_t color) {
    if (!ctx || !ctx->framebuffer) return;
    uint32_t *fb = (uint32_t *)ctx->framebuffer;
    
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            int32_t px = x + dx;
            int32_t py = y + dy;
            if (px >= 0 && px < (int32_t)ctx->width && py >= 0 && py < (int32_t)ctx->height) {
                fb[py * ctx->width + px] = color;
            }
        }
    }
}

/* ==================== 系统信息服务 ==================== */

static char system_hostname[64] = "funsos";

int sys_info_get(sys_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(sys_info_t));
    strcpy(info->os_name, "FunsOS");
    strcpy(info->kernel_version, "0.4");
    strcpy(info->hostname, system_hostname);
    
    extern uint64_t timer_get_ticks(void);
    info->uptime_seconds = timer_get_ticks() / 100; /* 假设100Hz */
    
    sys_mem_get_stats(&info->total_memory, NULL, &info->free_memory);
    info->cpu_count = 1;
    info->cpu_usage_percent = 0; /* TODO */
    info->process_count = 0; /* TODO */
    return 0;
}

int sys_info_get_hostname(char *hostname, uint32_t max_len) {
    if (!hostname) return -1;
    strncpy(hostname, system_hostname, max_len - 1);
    hostname[max_len - 1] = '\0';
    return 0;
}

int sys_info_set_hostname(const char *hostname) {
    if (!hostname) return -1;
    strncpy(system_hostname, hostname, 63);
    system_hostname[63] = '\0';
    return 0;
}

/* ==================== 通知服务 ==================== */

int sys_notify_send(const char *title, const char *message, sys_notify_type_t type) {
    klog_info("Notification: %s - %s (type=%d)", title, message, type);
    return notification_send(title, message, (uint32_t)type);
}

/* ==================== 应用服务 - 深度集成 OS 模块 ==================== */

int sys_filemanager_open(const char *path) {
    /* 使用文件管理器模块打开指定路径 */
    if (!file_manager_init()) {
        klog_err("Failed to initialize file manager");
        return -1;
    }
    /* 文件管理器初始化后会自动显示当前目录 */
    (void)path;
    klog_info("File manager opened for path: %s", path);
    return 0;
}

int sys_texteditor_open(const char *file) {
    /* 使用文本编辑器模块打开文件 */
    if (!text_editor_init()) {
        klog_err("Failed to initialize text editor");
        return -1;
    }
    if (file && file[0]) {
        return text_editor_open(file);
    }
    return 0;
}

int sys_terminal_spawn(void) {
    /* 创建新终端窗口 - 通过 terminal_main 启动 */
    if (terminal_main(0, NULL) != 0) {
        klog_err("Failed to spawn terminal");
        return -1;
    }
    return 0;
}

int sys_calculator_open(void) {
    /* 打开计算器应用 */
    if (!calculator_init()) {
        klog_err("Failed to initialize calculator");
        return -1;
    }
    klog_info("Calculator opened");
    return 0;
}

int sys_calculator_key_press(uint32_t key) {
    /* 向计算器发送按键 */
    return calculator_key_press(key);
}

const char *sys_calculator_get_display(void) {
    /* 获取计算器显示内容 */
    return calculator_get_display();
}

int sys_paint_open(void) {
    /* 打开画图应用 */
    if (!paint_init()) {
        klog_err("Failed to initialize paint");
        return -1;
    }
    klog_info("Paint opened");
    return 0;
}

int sys_paint_new_canvas(uint32_t width, uint32_t height) {
    return paint_new_canvas(width, height);
}

/* ==================== 其他服务 - 深度集成 OS 模块 ==================== */

int sys_taskbar_init(void) {
    /* 从系统状态获取屏幕尺寸 */
    extern desktop_service_t *sys_desktop_get_state(void);
    desktop_service_t *ds = sys_desktop_get_state();
    int w = ds ? (int)ds->screen_width : 1024;
    int h = ds ? (int)ds->screen_height : 768;
    taskbar_init(w, h, NULL, 0);
    return 0;
}

int sys_startmenu_init(void) {
    extern desktop_service_t *sys_desktop_get_state(void);
    desktop_service_t *ds = sys_desktop_get_state();
    int w = ds ? (int)ds->screen_width : 1024;
    int h = ds ? (int)ds->screen_height : 768;
    start_menu_init(w, h, NULL, 0);
    return 0;
}

int sys_audio_init(void) {
    /* TODO: 集成音频驱动初始化 */
    klog_info("Audio service initialized via system_services");
    return 0;
}

/* ==================== 电源管理服务 ==================== */

int sys_power_init(void) {
    return power_service_init();
}

int sys_power_start(void) {
    return power_service_start();
}

int sys_power_action(uint32_t action) {
    return power_service_action(action);
}

int sys_power_get_battery(int *percent, int *charging, int *remaining_minutes) {
    return power_service_get_battery(percent, charging, remaining_minutes);
}

int sys_power_set_brightness(int percent) {
    return power_service_set_brightness(percent);
}

int sys_power_get_brightness(void) {
    return power_service_get_brightness();
}

int sys_power_inhibit(const char *reason) {
    return power_service_inhibit(reason);
}

int sys_power_uninhibit(void) {
    return power_service_uninhibit();
}

/* ==================== 剪贴板服务 ==================== */

int sys_clipboard_init(void) {
    return clipboard_service_init();
}

int sys_clipboard_set_text(const char *text) {
    return clipboard_set_text(text);
}

int sys_clipboard_get_text(char *buffer, uint32_t max_len) {
    return clipboard_get_text(buffer, max_len);
}

int sys_clipboard_clear(void) {
    return clipboard_clear();
}

int sys_clipboard_is_empty(void) {
    return clipboard_is_empty();
}

int sys_clipboard_has_type(uint32_t type) {
    return clipboard_has_type(type);
}

int sys_clipboard_set_data(uint32_t type, const void *data, uint32_t size) {
    return clipboard_set_data(type, data, size);
}

int sys_clipboard_get_data(uint32_t *type, void *buf, uint32_t *size) {
    return clipboard_get_data(type, buf, size);
}

int sys_clipboard_begin_drag(int x, int y, uint32_t type, const void *data, uint32_t size) {
    return clipboard_begin_drag(x, y, type, data, size);
}

int sys_clipboard_end_drag(int *target_x, int *target_y) {
    return clipboard_end_drag(target_x, target_y);
}

int sys_clipboard_is_dragging(void) {
    return clipboard_is_dragging();
}

/* ==================== 系统服务更新 ==================== */

void sys_services_update(void) {
    /* 周期性更新所有服务状态 */
    power_service_update();
    clipboard_service_update();
}
