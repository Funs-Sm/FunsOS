#ifndef OOM_KILLER_H
#define OOM_KILLER_H

#include "stdint.h"
#include "kernel_types.h"

#define OOM_SCORE_ADJ_MIN (-1000)
#define OOM_SCORE_ADJ_MAX 1000
#define OOM_DISABLE (-17)

#define OOM_PAGE_ALLOC_RETRY_WARN (16)
#define OOM_PAGE_ALLOC_RETRY_FAIL (32)

typedef enum {
    OOM_TRIGGER_PAGE_ALLOC = 0,
    OOM_TRIGGER_KHEAP_ALLOC = 1,
    OOM_TRIGGER_MANUAL = 2
} oom_trigger_t;

struct oom_score {
    uint32_t pid;
    int32_t  score;
    int32_t  adj;
    uint32_t rss_pages;
    uint32_t cpu_time;
    uint32_t run_time;
    uint32_t nice;
    uint8_t  is_root;
    char     name[32];
};

void oom_init(void);
void oom_kill_process(uint32_t pid, const char *message);
int oom_kill_best_task(oom_trigger_t trigger);
void oom_out_of_memory(oom_trigger_t trigger, uint32_t order);
int32_t oom_evaluate_task(pcb_t *proc);
int oom_set_adj(uint32_t pid, int32_t adj);
int32_t oom_get_adj(uint32_t pid);
void oom_update_score(pcb_t *proc);
void oom_print_score_table(void);
uint8_t oom_is_triggered(void);
uint64_t oom_kill_count(void);

#endif
