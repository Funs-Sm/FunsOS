#include "battery.h"
#include "acpi.h"
#include "io.h"
#include "string.h"
#include "kheap.h"
#include "vmm.h"

/* Battery status flags from _BST */
#define BATTERY_DISCHARGING  0x01
#define BATTERY_CHARGING     0x02
#define BATTERY_CRITICAL     0x04

/* Battery technology types from _BIF */
#define BATTERY_TYPE_PRIMARY   0  /* Non-rechargeable */
#define BATTERY_TYPE_SECONDARY 1  /* Rechargeable */

/* Embedded Controller port addresses (common) */
#define EC_DATA_PORT    0x62
#define EC_CMD_PORT     0x66

/* EC commands */
#define EC_CMD_READ     0x80
#define EC_CMD_WRITE    0x81
#define EC_CMD_QUERY    0x84

/* IBF (Input Buffer Full) and OBF (Output Buffer Full) flags */
#define EC_IBF          0x02
#define EC_OBF          0x01

static battery_info_t battery_state;
static int battery_available = 0;

/* Simplified ACPI namespace object for battery device.
   In a full implementation, this would walk the ACPI namespace.
   Here we use a simplified approach: read from known I/O ports
   or from the embedded controller. */

/* Wait for EC input buffer to be empty */
static int ec_wait_ibf(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(EC_CMD_PORT) & EC_IBF)) return 0;
    }
    return -1;
}

/* Wait for EC output buffer to be full */
static int ec_wait_obf(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(EC_CMD_PORT) & EC_OBF) return 0;
    }
    return -1;
}

/* Read a byte from the Embedded Controller */
static int ec_read(uint8_t addr, uint8_t *data) {
    if (ec_wait_ibf() != 0) return -1;
    outb(EC_CMD_PORT, EC_CMD_READ);

    if (ec_wait_ibf() != 0) return -1;
    outb(EC_DATA_PORT, addr);

    if (ec_wait_obf() != 0) return -1;
    *data = inb(EC_DATA_PORT);
    return 0;
}

/* Try to read battery info from Embedded Controller.
   This uses common EC register offsets found on many laptops. */
static int battery_read_ec(battery_info_t *info) {
    uint8_t val;

    /* Try to read battery status from EC */
    /* Common EC registers (these vary by manufacturer): */
    /* 0x40-0x4F: Battery status area on many ECs */
    /* 0xA0: Battery percentage on some ECs */

    /* Try reading battery percentage */
    if (ec_read(0xA0, &val) == 0 && val <= 100) {
        info->percent = val;
        info->present = 1;
    } else if (ec_read(0x40, &val) == 0 && val <= 100) {
        info->percent = val;
        info->present = 1;
    } else {
        /* Could not read from EC */
        return -1;
    }

    /* Try to read battery status */
    if (ec_read(0x41, &val) == 0) {
        if (val & 0x01) {
            info->discharging = 1;
            strcpy(info->status, "Discharging");
        } else if (val & 0x02) {
            info->charging = 1;
            strcpy(info->status, "Charging");
        } else {
            strcpy(info->status, "Full");
        }
        if (val & 0x04) {
            info->critical = 1;
        }
    }

    /* Try to read voltage (2 bytes, big-endian) */
    uint8_t v_hi, v_lo;
    if (ec_read(0x42, &v_hi) == 0 && ec_read(0x43, &v_lo) == 0) {
        info->voltage = (uint16_t)((v_hi << 8) | v_lo);
    }

    /* Try to read current (2 bytes) */
    if (ec_read(0x44, &v_hi) == 0 && ec_read(0x45, &v_lo) == 0) {
        info->current = (uint16_t)((v_hi << 8) | v_lo);
    }

    /* Try to read remaining capacity */
    if (ec_read(0x46, &v_hi) == 0 && ec_read(0x47, &v_lo) == 0) {
        info->capacity = (uint16_t)((v_hi << 8) | v_lo);
    }

    /* Try to read design capacity */
    if (ec_read(0x48, &v_hi) == 0 && ec_read(0x49, &v_lo) == 0) {
        info->design_cap = (uint16_t)((v_hi << 8) | v_lo);
    }

    /* Default battery type */
    strcpy(info->type, "Li-ion");

    return 0;
}

/* Try to find battery device in ACPI namespace.
   This is a simplified approach - a full implementation would
   parse the DSDT/SSDT and evaluate AML methods. */
static int battery_find_acpi(void) {
    /* Check if FADT exists - if so, ACPI is available */
    facp_t *fadt = acpi_get_fadt();
    if (!fadt) return -1;

    /* In a full implementation, we would:
       1. Walk the ACPI namespace to find device with _HID = "PNP0C0A"
       2. Evaluate _STA to check if battery is present
       3. Evaluate _BIF for static battery info
       4. Evaluate _BST for current battery status

       Since we don't have an AML interpreter, we use the EC approach
       as a fallback. */

    return -1;
}

void battery_init(void) {
    memset(&battery_state, 0, sizeof(battery_info_t));

    /* Try ACPI first */
    if (battery_find_acpi() == 0) {
        battery_available = 1;
        return;
    }

    /* Fall back to Embedded Controller */
    if (battery_read_ec(&battery_state) == 0) {
        battery_available = 1;
        return;
    }

    /* No battery available */
    battery_available = 0;
}

int battery_get_info(battery_info_t *info) {
    if (!info) return -1;

    if (!battery_available) {
        memset(info, 0, sizeof(battery_info_t));
        return -1;
    }

    /* Refresh battery state from EC */
    if (battery_read_ec(&battery_state) != 0) {
        /* EC read failed, return cached state */
    }

    /* Copy current state to caller */
    memcpy(info, &battery_state, sizeof(battery_info_t));
    return 0;
}

uint8_t battery_get_percent(void) {
    if (!battery_available) return 0;

    /* Try to refresh */
    battery_info_t tmp;
    if (battery_get_info(&tmp) == 0) {
        return tmp.percent;
    }
    return battery_state.percent;
}

int battery_is_charging(void) {
    if (!battery_available) return 0;

    battery_info_t tmp;
    if (battery_get_info(&tmp) == 0) {
        return tmp.charging;
    }
    return battery_state.charging;
}
