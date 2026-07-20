#ifndef SYSRQ_H
#define SYSRQ_H

#include "stdint.h"

#define SYSRQ_MAX_KEYS 32

typedef void (*sysrq_handler_t)(uint8_t key);

struct sysrq_key_op {
    uint8_t key;
    sysrq_handler_t handler;
    const char *help_msg;
    const char *action_msg;
};

void sysrq_init(void);
int sysrq_register_key(uint8_t key, sysrq_handler_t handler, const char *help, const char *action);
int sysrq_unregister_key(uint8_t key);
void sysrq_handle_key(uint8_t key);
void sysrq_toggle_enable(void);
uint8_t sysrq_is_enabled(void);
void sysrq_print_help(uint8_t key);

void sysrq_trigger_crash(uint8_t key);
void sysrq_show_mem(uint8_t key);
void sysrq_show_tasks(uint8_t key);
void sysrq_show_regs(uint8_t key);
void sysrq_sync_fs(uint8_t key);
void sysrq_unmount_raw(uint8_t key);
void sysrq_reboot(uint8_t key);
void sysrq_show_state(uint8_t key);
void sysrq_show_timers(uint8_t key);
void sysrq_show_locks(uint8_t key);
void sysrq_kill_all(uint8_t key);
void sysrq_nice_all(uint8_t key);

#endif
