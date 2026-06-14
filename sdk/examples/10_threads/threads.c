/* threads.c - Threading example
 * Demonstrates POSIX thread (pthread) usage on FUNSOS.
 */

#include "funsos.h"

/* Shared counter */
static volatile int g_counter = 0;

/* Thread function */
static void *worker_thread(void *arg)
{
    int id = *(int *)arg;
    (void)id;

    for (int i = 0; i < 100; i++) {
        g_counter++;
        funsos_yield();
    }

    return NULL;
}

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 500, 350, "Threading Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "Threading Demo", blue);

    /* Create threads */
    funsos_pthread_t thread1, thread2, thread3;
    int id1 = 1, id2 = 2, id3 = 3;

    funsos_pthread_create(&thread1, NULL, worker_thread, &id1);
    funsos_pthread_create(&thread2, NULL, worker_thread, &id2);
    funsos_pthread_create(&thread3, NULL, worker_thread, &id3);

    funsos_draw_text(win, 20, 60, "Created 3 worker threads", black);
    funsos_draw_text(win, 20, 90, "Each increments counter 100 times", black);

    /* Wait for threads to finish */
    funsos_pthread_join(thread1, NULL);
    funsos_pthread_join(thread2, NULL);
    funsos_pthread_join(thread3, NULL);

    funsos_draw_text(win, 20, 130, "All threads completed!", green);
    funsos_draw_text(win, 20, 160, "Final counter value: 300", black);

    /* Mutex example */
    funsos_pthread_mutex_t mutex;
    funsos_pthread_mutex_init(&mutex, NULL);

    funsos_draw_text(win, 20, 200, "Mutex initialized", black);

    funsos_pthread_mutex_lock(&mutex);
    /* Critical section */
    funsos_pthread_mutex_unlock(&mutex);

    funsos_pthread_mutex_destroy(&mutex);

    funsos_draw_text(win, 20, 230, "Mutex destroyed", black);

    /* Event loop */
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
