#ifndef ACPI_SLEEP_H
#define ACPI_SLEEP_H

#include "stdint.h"

int acpi_enter_sleep(uint8_t state);
int acpi_leave_sleep(uint8_t state);
int acpi_suspend(void);
int acpi_resume(void);
int acpi_shutdown(void);
int acpi_reboot(void);

#endif
