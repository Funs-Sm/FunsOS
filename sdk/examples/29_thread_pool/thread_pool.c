/* thread_pool.c - 线程池示例
 * 演示 FUNSOS SDK v1.5.0 线程池实现。
 */

#include "funsos.h"
#include "funsos_thread.h"

/* 任务结构 */
typedef struct {
    void (*func)(void *arg);
    void *arg;
    int   done;
} task_t;

/* 线程池结构 */
#define MAX_TASKS  32
#define MAX_THREADS 4

typedef struct {
    funsos_thread_t *threads[MAX_THREADS];
    int             thread_count;

    task_t          tasks[MAX_TASKS];
    int             task_head;
    int             task_tail;
    int             task_count;

    funsos_mutex_t  mutex;
    funsos_cond_t   not_empty;
    funsos_cond_t   not_full;

    int             shutdown;
} thread_pool_t;

static thread_pool_t g_pool;

/* 工作线程函数 */
static void *worker_thread(void *arg)
{
    int id = *(int *)arg;
    (void)id;

    while (1) {
        funsos_mutex_lock(&g_pool.mutex);

        while (g_pool.task_count == 0 && !g_pool.shutdown) {
            funsos_cond_wait(&g_pool.not_empty, &g_pool.mutex);
        }

        if (g_pool.shutdown && g_pool.task_count == 0) {
            funsos_mutex_unlock(&g_pool.mutex);
            break;
        }

        /* 取出任务 */
        task_t *task = &g_pool.tasks[g_pool.task_head];
        g_pool.task_head = (g_pool.task_head + 1) % MAX_TASKS;
        g_pool.task_count--;

        funsos_cond_signal(&g_pool.not_full);
        funsos_mutex_unlock(&g_pool.mutex);

        /* 执行任务 */
        if (task->func) {
            task->func(task->arg);
            task->done = 1;
        }
    }

    return NULL;
}

/* 初始化线程池 */
static int thread_pool_init(int num_threads)
{
    if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;

    funsos_mutex_init(&g_pool.mutex, FUNSOS_MUTEX_DEFAULT);
    funsos_cond_init(&g_pool.not_empty);
    funsos_cond_init(&g_pool.not_full);

    g_pool.task_head = 0;
    g_pool.task_tail = 0;
    g_pool.task_count = 0;
    g_pool.shutdown = 0;
    g_pool.thread_count = num_threads;

    for (int i = 0; i < num_threads; i++) {
        static int ids[MAX_THREADS];
        ids[i] = i;
        funsos_thread_create(&g_pool.threads[i], NULL, worker_thread, &ids[i]);
    }

    return 0;
}

/* 提交任务 */
static int thread_pool_submit(void (*func)(void *), void *arg)
{
    funsos_mutex_lock(&g_pool.mutex);

    while (g_pool.task_count == MAX_TASKS && !g_pool.shutdown) {
        funsos_cond_wait(&g_pool.not_full, &g_pool.mutex);
    }

    if (g_pool.shutdown) {
        funsos_mutex_unlock(&g_pool.mutex);
        return -1;
    }

    task_t *task = &g_pool.tasks[g_pool.task_tail];
    task->func = func;
    task->arg = arg;
    task->done = 0;
    g_pool.task_tail = (g_pool.task_tail + 1) % MAX_TASKS;
    g_pool.task_count++;

    funsos_cond_signal(&g_pool.not_empty);
    funsos_mutex_unlock(&g_pool.mutex);

    return 0;
}

/* 销毁线程池 */
static void thread_pool_shutdown(void)
{
    funsos_mutex_lock(&g_pool.mutex);
    g_pool.shutdown = 1;
    funsos_cond_broadcast(&g_pool.not_empty);
    funsos_mutex_unlock(&g_pool.mutex);

    for (int i = 0; i < g_pool.thread_count; i++) {
        funsos_thread_join(g_pool.threads[i], NULL);
    }

    funsos_mutex_destroy(&g_pool.mutex);
    funsos_cond_destroy(&g_pool.not_empty);
    funsos_cond_destroy(&g_pool.not_full);
}

/* 示例任务：计算平方 */
static int g_results[10];

static void square_task(void *arg)
{
    int n = *(int *)arg;
    g_results[n] = n * n;
}

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 550, 400, "线程池示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - 线程池示例", blue);
    funsos_draw_text(win, 20, 45, "创建 4 个工作线程的线程池", black);

    /* 初始化线程池 */
    thread_pool_init(4);
    funsos_draw_text(win, 20, 80, "[OK] 线程池初始化完成 (4 线程)", green);

    /* 提交任务 */
    int nums[10];
    for (int i = 0; i < 10; i++) {
        nums[i] = i;
        thread_pool_submit(square_task, &nums[i]);
    }
    funsos_draw_text(win, 20, 110, "[OK] 提交 10 个计算任务", green);

    /* 等待任务完成 */
    funsos_sleep(1);

    /* 显示结果 */
    funsos_draw_text(win, 20, 150, "计算结果 (n * n):", blue);

    char buf[64];
    for (int i = 0; i < 10; i++) {
        funsos_snprintf(buf, sizeof(buf), "  %d * %d = %d", i, i, g_results[i]);
        funsos_draw_text(win, 40, 175 + i * 20, buf, black);
    }

    /* 关闭线程池 */
    thread_pool_shutdown();
    funsos_draw_text(win, 20, 370, "[OK] 线程池已关闭", green);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
