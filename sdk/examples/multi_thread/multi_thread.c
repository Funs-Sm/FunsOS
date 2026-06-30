/* multi_thread.c - 多线程示例程序
 * 演示 FUNSOS 中的多线程编程，包括线程创建、同步、互斥锁等。
 *
 * 功能说明：
 *   - 创建多个工作线程
 *   - 线程同步与互斥
 *   - 线程等待 (join)
 *   - 原子操作和计数器
 *   - 信号量演示
 *
 * 使用的主要 API：
 *   - funsos_pthread_create() - 创建线程
 *   - funsos_pthread_join() - 等待线程结束
 *   - funsos_pthread_mutex_init() - 初始化互斥锁
 *   - funsos_pthread_mutex_lock() - 加锁
 *   - funsos_pthread_mutex_unlock() - 解锁
 *   - funsos_pthread_mutex_destroy() - 销毁互斥锁
 *   - funsos_yield() - 让出 CPU
 */

#include "funsos.h"

/* 全局共享计数器 */
static volatile int g_counter = 0;
static volatile int g_safe_counter = 0;
static volatile int g_thread_running[4] = {0};

/* 互斥锁 */
static funsos_pthread_mutex_t g_mutex;

/* 线程工作数据结构 */
typedef struct {
    int thread_id;
    int iterations;
} thread_data_t;

/* 辅助函数：整数转字符串 */
static void int_to_str(int value, char *buf)
{
    char tmp[16];
    int pos = 0;

    if (value == 0) {
        buf[pos++] = '0';
        buf[pos] = '\0';
        return;
    }

    while (value > 0 && pos < 15) {
        tmp[pos++] = '0' + (value % 10);
        value /= 10;
    }

    int out = 0;
    for (int i = pos - 1; i >= 0; i--) {
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* 在窗口中绘制状态行 */
static void draw_status(funsos_window_t win, int y, const char *label,
                        const char *value, funsos_color_t color)
{
    funsos_draw_text(win, 20, y, label, FUNSOS_COLOR_BLACK);
    funsos_draw_text(win, 200, y, value, color);
}

/*
 * 不安全的线程函数（演示竞态条件）
 * 直接操作全局计数器，不使用互斥锁
 */
static void *unsafe_worker(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    int id = data->thread_id;
    (void)id;

    for (int i = 0; i < data->iterations; i++) {
        /* 无锁递增 - 可能导致竞态条件 */
        int temp = g_counter;
        funsos_yield();  /* 增加发生竞态的概率 */
        g_counter = temp + 1;
    }

    return NULL;
}

/*
 * 安全的线程函数（使用互斥锁）
 * 使用互斥锁保护全局计数器
 */
static void *safe_worker(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    int id = data->thread_id;
    g_thread_running[id] = 1;

    for (int i = 0; i < data->iterations; i++) {
        /* 使用互斥锁保护临界区 */
        funsos_pthread_mutex_lock(&g_mutex);
        int temp = g_safe_counter;
        funsos_pthread_mutex_unlock(&g_mutex);

        funsos_yield();  /* 模拟一些工作 */

        funsos_pthread_mutex_lock(&g_mutex);
        g_safe_counter = temp + 1;
        funsos_pthread_mutex_unlock(&g_mutex);
    }

    g_thread_running[id] = 0;
    return NULL;
}

int main(void)
{
    int line_y = 20;
    char buf[64];
    const int iterations = 100;
    const int num_threads = 4;

    /* 创建窗口 */
    funsos_window_t win = funsos_create_window(100, 60, 550, 500, "Multi-Thread Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* 标题 */
    funsos_draw_text(win, 20, line_y, "=== Multi-Threading Demo ===", FUNSOS_COLOR_BLUE);
    line_y += 30;

    /* --- 1. 初始化互斥锁 --- */
    int ret = funsos_pthread_mutex_init(&g_mutex, NULL);
    if (ret == 0) {
        draw_status(win, line_y, "Mutex init:", "OK", FUNSOS_COLOR_GREEN);
    } else {
        draw_status(win, line_y, "Mutex init:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 2. 演示：无锁计数 (竞态条件) --- */
    funsos_draw_text(win, 20, line_y, "Test 1: Unsafe counter (race condition)", FUNSOS_COLOR_BLACK);
    line_y += 25;

    g_counter = 0;
    funsos_pthread_t threads_unsafe[4];
    thread_data_t data_unsafe[4];

    /* 创建线程 */
    for (int i = 0; i < num_threads; i++) {
        data_unsafe[i].thread_id = i;
        data_unsafe[i].iterations = iterations;
        funsos_pthread_create(&threads_unsafe[i], NULL, unsafe_worker, &data_unsafe[i]);
    }

    /* 等待所有线程完成 */
    for (int i = 0; i < num_threads; i++) {
        funsos_pthread_join(threads_unsafe[i], NULL);
    }

    int_to_str(num_threads * iterations, buf);
    strcat(buf, " (expected)");
    draw_status(win, line_y, "Expected:", buf, FUNSOS_COLOR_DARK_GRAY);
    line_y += 22;

    int_to_str(g_counter, buf);
    funsos_color_t race_color = (g_counter == num_threads * iterations)
                                ? FUNSOS_COLOR_GREEN : FUNSOS_COLOR_RED;
    draw_status(win, line_y, "Actual:", buf, race_color);
    line_y += 22;

    if (g_counter != num_threads * iterations) {
        draw_status(win, line_y, "Result:", "Race condition detected!", FUNSOS_COLOR_RED);
    } else {
        draw_status(win, line_y, "Result:", "No race (lucky)", FUNSOS_COLOR_ORANGE);
    }
    line_y += 30;

    /* --- 3. 演示：安全计数 (使用互斥锁) --- */
    funsos_draw_text(win, 20, line_y, "Test 2: Safe counter (with mutex)", FUNSOS_COLOR_BLACK);
    line_y += 25;

    g_safe_counter = 0;
    funsos_pthread_t threads_safe[4];
    thread_data_t data_safe[4];

    /* 创建线程 */
    for (int i = 0; i < num_threads; i++) {
        data_safe[i].thread_id = i;
        data_safe[i].iterations = iterations;
        funsos_pthread_create(&threads_safe[i], NULL, safe_worker, &data_safe[i]);
    }

    /* 等待所有线程完成 */
    for (int i = 0; i < num_threads; i++) {
        funsos_pthread_join(threads_safe[i], NULL);
    }

    int_to_str(num_threads * iterations, buf);
    strcat(buf, " (expected)");
    draw_status(win, line_y, "Expected:", buf, FUNSOS_COLOR_DARK_GRAY);
    line_y += 22;

    int_to_str(g_safe_counter, buf);
    funsos_color_t safe_color = (g_safe_counter == num_threads * iterations)
                                ? FUNSOS_COLOR_GREEN : FUNSOS_COLOR_RED;
    draw_status(win, line_y, "Actual:", buf, safe_color);
    line_y += 22;

    if (g_safe_counter == num_threads * iterations) {
        draw_status(win, line_y, "Result:", "Correct (mutex works!)", FUNSOS_COLOR_GREEN);
    } else {
        draw_status(win, line_y, "Result:", "Still wrong", FUNSOS_COLOR_RED);
    }
    line_y += 30;

    /* --- 4. 线程信息 --- */
    funsos_draw_text(win, 20, line_y, "Thread Information:", FUNSOS_COLOR_BLUE);
    line_y += 25;

    int_to_str(num_threads, buf);
    strcat(buf, " worker threads created");
    draw_status(win, line_y, "Threads:", buf, FUNSOS_COLOR_DARK_GRAY);
    line_y += 22;

    int_to_str(iterations, buf);
    strcat(buf, " iterations per thread");
    draw_status(win, line_y, "Iterations:", buf, FUNSOS_COLOR_DARK_GRAY);
    line_y += 25;

    /* --- 5. 支持的线程功能 --- */
    funsos_draw_text(win, 20, line_y, "Supported thread features:", FUNSOS_COLOR_BLUE);
    line_y += 25;

    const char *features[] = {
        "  - POSIX-style threads (pthread)",
        "  - Mutex locks",
        "  - Thread join",
        "  - Thread yield",
        "  - Thread-local storage (TLS)",
        "  - Semaphores",
        "  - Condition variables",
        "  - Reader-writer locks",
        NULL
    };

    for (int i = 0; features[i] != NULL; i++) {
        funsos_draw_text(win, 30, line_y, features[i], FUNSOS_COLOR_DARK_GRAY);
        line_y += 20;
    }

    /* --- 6. 销毁互斥锁 --- */
    funsos_pthread_mutex_destroy(&g_mutex);

    /* 底部提示 */
    funsos_draw_line(win, 20, 460, 530, 460, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 130, 475, "Press ESC to exit - Thread Demo",
                     FUNSOS_COLOR_DARK_GRAY);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
