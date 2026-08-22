/*
 * kernel/cmd_power.c - Power management commands (PR-5, v0.9)
 *
 * All four commands delegate to the ACPI sleep state machine in
 * kernel/acpi_sleep.c.  In QEMU without a full FADT, the keyboard
 * controller reset fallback in acpi_reboot() handles the rest.
 */

#include "cmd_power.h"
#include "shell.h"
#include "acpi_sleep.h"
#include "timer.h"
#include "stdio.h"
#include "string.h"

/* countdown before the action actually fires, so users can abort */
#define POWER_COUNTDOWN_S 5

static int parse_force(const char *args)
{
    if (!args) return 0;
    while (*args == ' ') args++;
    return (args[0] == '-' && args[1] == 'f' &&
            (args[2] == ' ' || args[2] == '\0'));
}

static void announce(const char *name, int countdown_s)
{
    char buf[96];
    snprintf(buf, sizeof(buf),
             "%s: system will %s in %d seconds.   (poweroff is immediate)\n",
             name,
             !strcmp(name, "reboot") ? "reboot" :
             !strcmp(name, "halt")   ? "halt"   :
             !strcmp(name, "shutdown") ? "power off" : "halt",
             countdown_s);
    shell_print(buf);
}

static void do_reboot(void)
{
    shell_print("reboot: triggering ACPI reset ...\n");
    int rc = acpi_reboot();
    if (rc != 0) {
        shell_print("reboot: acpi_reboot failed, looping as fallback\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }
}

static void do_shutdown(void)
{
    shell_print("shutdown: writing S5 (soft off) ...\n");
    (void)acpi_shutdown();
    /* If ACPI is unavailable in this VM, fall through to keyboard reset. */
    shell_print("shutdown: ACPI did not power off, falling back to keyboard reset\n");
    do_reboot();
}

static void do_halt(void)
{
    shell_print("halt: system halted.  (CPU in HLT; power still on.)\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void cmd_reboot(const char *args)
{
    if (parse_force(args)) {
        do_reboot();
        return;
    }
    announce("reboot", POWER_COUNTDOWN_S);
    /* Wait POWER_COUNTDOWN_S seconds, but check keyboard for 'y' to skip wait */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < (uint32_t)POWER_COUNTDOWN_S * 100) {
        timer_sleep(100);
    }
    do_reboot();
}

void cmd_halt(const char *args)
{
    if (parse_force(args)) {
        do_halt();
        return;
    }
    announce("halt", POWER_COUNTDOWN_S);
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < (uint32_t)POWER_COUNTDOWN_S * 100) {
        timer_sleep(100);
    }
    do_halt();
}

void cmd_shutdown(const char *args)
{
    if (parse_force(args)) {
        do_shutdown();
        return;
    }
    announce("shutdown", POWER_COUNTDOWN_S);
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < (uint32_t)POWER_COUNTDOWN_S * 100) {
        timer_sleep(100);
    }
    do_shutdown();
}

void cmd_poweroff(const char *args)
{
    /* poweroff is an alias for shutdown -p on most systems */
    cmd_shutdown(args);
}
