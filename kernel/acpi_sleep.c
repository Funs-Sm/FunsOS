#include "acpi_sleep.h"
#include "acpi.h"
#include "io.h"
#include "string.h"
#include "timer.h"

/* PM1 Control Register bits */
#define PM1_CNT_SLP_TYP_SHIFT  10
#define PM1_CNT_SLP_EN         (1 << 13)
#define PM1_CNT_SCI_EN         (1 << 0)

/* Sleep type values (from DSDT \_Sx packages) */
/* These are typical values; real values should be parsed from DSDT */
#define SLP_TYP_S0   0
#define SLP_TYP_S1   1
#define SLP_TYP_S3   5  /* Typical S3 sleep type */
#define SLP_TYP_S5   7  /* Typical S5 soft-off type */

/* Read from PM1a event register */
static inline uint16_t pm1a_evt_read(facp_t *fadt) {
    if (fadt->pm1a_evt_blk == 0) return 0;
    if (fadt->pm1_evt_len == 4) {
        return (uint16_t)inl(fadt->pm1a_evt_blk);
    }
    return inw(fadt->pm1a_evt_blk);
}

/* Write to PM1a control register */
static inline void pm1a_cnt_write(facp_t *fadt, uint16_t val) {
    if (fadt->pm1a_cnt_blk == 0) return;
    if (fadt->pm1_cnt_len == 4) {
        outl(fadt->pm1a_cnt_blk, val);
    } else {
        outw(fadt->pm1a_cnt_blk, val);
    }
}

/* Write to PM1b control register */
static inline void pm1b_cnt_write(facp_t *fadt, uint16_t val) {
    if (fadt->pm1b_cnt_blk == 0) return;
    if (fadt->pm1_cnt_len == 4) {
        outl(fadt->pm1b_cnt_blk, val);
    } else {
        outw(fadt->pm1b_cnt_blk, val);
    }
}

/* Clear PM1 status registers */
static void pm1_status_clear(facp_t *fadt) {
    /* Write 1 to clear all status bits */
    if (fadt->pm1a_evt_blk) {
        if (fadt->pm1_evt_len == 4) {
            outl(fadt->pm1a_evt_blk, 0x8000FFFF);
        } else {
            outw(fadt->pm1a_evt_blk, 0xFFFF);
        }
    }
    if (fadt->pm1b_evt_blk) {
        if (fadt->pm1_evt_len == 4) {
            outl(fadt->pm1b_evt_blk, 0x8000FFFF);
        } else {
            outw(fadt->pm1b_evt_blk, 0xFFFF);
        }
    }
}

/* Enable ACPI mode */
static int acpi_enable(facp_t *fadt) {
    /* Check if ACPI is already enabled */
    if (pm1a_evt_read(fadt) & PM1_CNT_SCI_EN) {
        return 0;  /* Already enabled */
    }

    /* Send ACPI_ENABLE to SMI_CMD */
    if (fadt->smi_cmd == 0) return -1;
    outb(fadt->smi_cmd, fadt->acpi_enable);

    /* Wait for SCI_EN to be set */
    uint32_t timeout = 5000;
    while (timeout--) {
        if (pm1a_evt_read(fadt) & PM1_CNT_SCI_EN) {
            return 0;
        }
        /* Small delay */
        for (volatile int i = 0; i < 100; i++);
    }
    return -1;
}

int acpi_enter_sleep(uint8_t state) {
    facp_t *fadt = acpi_get_fadt();
    if (!fadt) return -1;

    /* Ensure ACPI mode is enabled */
    if (acpi_enable(fadt) != 0) return -1;

    /* Determine SLP_TYP for the desired state */
    uint16_t slp_typ;
    switch (state) {
        case 1: slp_typ = SLP_TYP_S1; break;
        case 3: slp_typ = SLP_TYP_S3; break;
        case 5: slp_typ = SLP_TYP_S5; break;
        default: return -1;
    }

    /* Flush caches */
    asm volatile("wbinvd");

    /* Disable interrupts */
    asm volatile("cli");

    /* Clear PM1 status */
    pm1_status_clear(fadt);

    /* Build the sleep control value */
    uint16_t pm1a_cnt = (slp_typ << PM1_CNT_SLP_TYP_SHIFT) | PM1_CNT_SLP_EN;
    uint16_t pm1b_cnt = pm1a_cnt;

    /* Write SLP_TYP + SLP_EN to PM1 control registers */
    /* Must write both PM1a_CNT and PM1b_CNT simultaneously */
    pm1a_cnt_write(fadt, pm1a_cnt);
    pm1b_cnt_write(fadt, pm1b_cnt);

    /* If we get here, sleep failed - the CPU should have halted */
    /* Wait a bit and re-enable interrupts */
    for (volatile int i = 0; i < 1000000; i++);
    asm volatile("sti");

    return -1;
}

int acpi_leave_sleep(uint8_t state) {
    (void)state;

    /* Re-enable interrupts */
    asm volatile("sti");

    /* Re-initialize timer */
    init_timer();

    /* Clear PM1 status */
    facp_t *fadt = acpi_get_fadt();
    if (fadt) {
        pm1_status_clear(fadt);
    }

    return 0;
}

int acpi_suspend(void) {
    /* Enter S3 (Suspend to RAM) */
    int ret = acpi_enter_sleep(3);

    /* If we wake up from S3, resume */
    if (ret == -1) {
        /* Sleep entry failed or we woke up */
        acpi_leave_sleep(3);
    }

    return ret;
}

int acpi_resume(void) {
    /* Resume from S3 */
    return acpi_leave_sleep(3);
}

int acpi_shutdown(void) {
    /* Enter S5 (Soft Off) */
    return acpi_enter_sleep(5);
}

int acpi_reboot(void) {
    facp_t *fadt = acpi_get_fadt();

    /* Try ACPI reset register first (FADT reset_reg) */
    if (fadt && fadt->length >= 244 + 12) {
        /* Reset register is a Generic Address Structure at offset 244 in FADT
         * GAS format: address_space(1) + bit_width(1) + bit_offset(1) + access_size(1) + address(8)
         * address_space: 0=SystemIO, 1=SystemMemory */
        uint8_t addr_space = fadt->reset_reg[0];
        uint64_t address = 0;
        /* Read 64-bit address from bytes 4-11 of the GAS */
        for (int i = 0; i < 8; i++) {
            address |= ((uint64_t)fadt->reset_reg[4 + i]) << (i * 8);
        }

        if (address != 0) {
            if (addr_space == 0) {
                /* System I/O space */
                outb((uint16_t)address, fadt->reset_value);
            } else if (addr_space == 1) {
                /* System Memory space */
                *((volatile uint8_t *)(uint32_t)address) = fadt->reset_value;
            }
        }
    }

    /* Fallback: keyboard controller reset */
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    /* If that didn't work, triple fault */
    {
        struct __attribute__((packed)) {
            uint16_t limit;
            uint32_t base;
        } idtr_null = {0, 0};
        asm volatile(
            "lidt %0\n"
            "int $0x03\n"
            : : "m"(idtr_null)
        );
    }

    /* Should never reach here */
    while (1) {
        asm volatile("hlt");
    }
    return -1;
}
