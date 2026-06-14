#include "driver_manager.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

#define MAX_DRIVERS 64

static driver_info_t *driver_list[MAX_DRIVERS];
static uint32_t driver_count = 0;

void driver_manager_init(void) {
    memset(driver_list, 0, sizeof(driver_list));
    driver_count = 0;
}

int driver_register(driver_info_t *drv) {
    if (!drv) return -1;
    if (driver_count >= MAX_DRIVERS) return -1;

    /* Check for duplicate name */
    for (uint32_t i = 0; i < driver_count; i++) {
        if (driver_list[i] && strcmp(driver_list[i]->name, drv->name) == 0) {
            return -1; /* Already registered */
        }
    }

    drv->state = DRIVER_STATE_LOADING;

    /* Call driver init if provided */
    if (drv->init) {
        int ret = drv->init(drv);
        if (ret != 0) {
            drv->state = DRIVER_STATE_ERROR;
            return -1;
        }
    }

    drv->state = DRIVER_STATE_RUNNING;
    driver_list[driver_count] = drv;
    driver_count++;

    return 0;
}

int driver_unregister(const char *name) {
    if (!name) return -1;

    for (uint32_t i = 0; i < driver_count; i++) {
        if (driver_list[i] && strcmp(driver_list[i]->name, name) == 0) {
            driver_info_t *drv = driver_list[i];

            /* Call shutdown if provided */
            if (drv->shutdown) {
                drv->shutdown(drv);
            }

            drv->state = DRIVER_STATE_STOPPED;

            /* Shift remaining entries */
            for (uint32_t j = i; j < driver_count - 1; j++) {
                driver_list[j] = driver_list[j + 1];
            }
            driver_list[driver_count - 1] = NULL;
            driver_count--;

            return 0;
        }
    }

    return -1; /* Not found */
}

driver_info_t *driver_find_by_name(const char *name) {
    if (!name) return NULL;

    for (uint32_t i = 0; i < driver_count; i++) {
        if (driver_list[i] && strcmp(driver_list[i]->name, name) == 0) {
            return driver_list[i];
        }
    }
    return NULL;
}

driver_info_t *driver_find_by_pci(uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; i < driver_count; i++) {
        if (driver_list[i] &&
            driver_list[i]->vendor_id == vendor &&
            driver_list[i]->device_id == device) {
            return driver_list[i];
        }
    }
    return NULL;
}

driver_info_t *driver_find_by_class(uint8_t cls, uint8_t subclass) {
    for (uint32_t i = 0; i < driver_count; i++) {
        if (driver_list[i] &&
            driver_list[i]->pci_class == cls &&
            driver_list[i]->pci_subclass == subclass) {
            return driver_list[i];
        }
    }
    return NULL;
}

uint32_t driver_get_count(void) {
    return driver_count;
}

driver_info_t *driver_get_by_index(uint32_t index) {
    if (index >= driver_count) return NULL;
    return driver_list[index];
}

static const char *driver_type_name(uint32_t type) {
    switch (type) {
        case DRIVER_TYPE_BLOCK:   return "block";
        case DRIVER_TYPE_CHAR:    return "char";
        case DRIVER_TYPE_NET:     return "net";
        case DRIVER_TYPE_AUDIO:   return "audio";
        case DRIVER_TYPE_GPU:     return "gpu";
        case DRIVER_TYPE_USB_HCD: return "usb-hcd";
        case DRIVER_TYPE_INPUT:   return "input";
        default:                  return "unknown";
    }
}

static const char *driver_state_name(uint32_t state) {
    switch (state) {
        case DRIVER_STATE_UNUSED:   return "unused";
        case DRIVER_STATE_LOADING:  return "loading";
        case DRIVER_STATE_RUNNING:  return "running";
        case DRIVER_STATE_ERROR:    return "error";
        case DRIVER_STATE_STOPPED:  return "stopped";
        default:                    return "???";
    }
}

void driver_list_all(void) {
    printf("Registered drivers (%u):\n", driver_count);
    for (uint32_t i = 0; i < driver_count; i++) {
        driver_info_t *drv = driver_list[i];
        if (!drv) continue;

        printf("  [%u] %-16s %-8s %-8s %04X:%04X cls=%02X/%02X/%02X pci=%u:%u:%u\n",
               i,
               drv->name,
               driver_type_name(drv->type),
               driver_state_name(drv->state),
               drv->vendor_id,
               drv->device_id,
               drv->pci_class,
               drv->pci_subclass,
               drv->pci_prog_if,
               drv->pci_bus,
               drv->pci_dev,
               drv->pci_func);

        if (drv->description[0]) {
            printf("      %s\n", drv->description);
        }
    }
}
