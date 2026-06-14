#include "acpi.h"
#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"

static rsdp_t *rsdp = 0;
static rsdt_t *rsdt = 0;
static madt_t *madt = 0;
static facp_t *fadt = 0;
static uint32_t lapic_base = 0;
static uint32_t cpu_count = 0;
static uint32_t ioapic_base = 0;
static uint8_t lapic_ids[256];

static uint8_t acpi_checksum(uint8_t *ptr, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    return sum;
}

rsdp_t *acpi_find_rsdp(void) {
    uint32_t ebda = *(uint16_t *)0x40E;
    ebda <<= 4;
    uint32_t search_addrs[] = {ebda, 0xE0000};
    uint32_t search_ends[] = {ebda + 0x400, 0xFFFFF};

    for (int range = 0; range < 2; range++) {
        for (uint32_t addr = search_addrs[range]; addr < search_ends[range]; addr += 16) {
            rsdp_t *candidate = (rsdp_t *)addr;
            if (memcmp(candidate->signature, "RSD PTR ", 8) == 0) {
                if (acpi_checksum((uint8_t *)candidate, 20) == 0) {
                    return candidate;
                }
            }
        }
    }
    return 0;
}

void acpi_parse_rsdt(void) {
    if (!rsdp) return;

    rsdt = (rsdt_t *)(uint32_t)rsdp->rsdt_address;
    uint32_t rsdt_virt = (uint32_t)rsdt;
    if (rsdt_virt < VMM_KERNEL_BASE) {
        uint32_t rsdt_phys = rsdt_virt;
        rsdt_virt = rsdt_phys + VMM_KERNEL_BASE;
        vmm_map_page(0, rsdt_virt, rsdt_phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        rsdt = (rsdt_t *)rsdt_virt;
    }

    if (memcmp(rsdt->signature, "RSDT", 4) != 0) {
        rsdt = 0;
        return;
    }

    if (acpi_checksum((uint8_t *)rsdt, rsdt->length) != 0) {
        rsdt = 0;
        return;
    }
}

void acpi_parse_madt(void) {
    if (!rsdt) return;

    uint32_t entry_count = (rsdt->length - sizeof(rsdt_t)) / sizeof(uint32_t);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t entry_addr = rsdt->entries[i];
        uint32_t entry_virt = entry_addr;
        if (entry_virt < VMM_KERNEL_BASE) {
            entry_virt = entry_addr + VMM_KERNEL_BASE;
        }

        madt_t *candidate = (madt_t *)entry_virt;
        if (memcmp(candidate->signature, "APIC", 4) == 0) {
            madt = candidate;
            lapic_base = madt->local_apic_address;
            cpu_count = 0;

            uint32_t offset = 0;
            uint32_t madt_entries_len = madt->length - sizeof(madt_t);
            while (offset < madt_entries_len) {
                uint8_t *entry_ptr = (uint8_t *)madt->entries + offset;
                uint8_t type = entry_ptr[0];
                uint8_t length = entry_ptr[1];

                if (length == 0) break;

                if (type == 0) {
                    madt_entry_lapic_t *lapic = (madt_entry_lapic_t *)entry_ptr;
                    if (lapic->flags & 0x01) {
                        lapic_ids[cpu_count] = lapic->apic_id;
                        cpu_count++;
                    }
                } else if (type == 1) {
                    madt_entry_ioapic_t *ioapic = (madt_entry_ioapic_t *)entry_ptr;
                    ioapic_base = ioapic->ioapic_address;
                }

                offset += length;
            }
            break;
        }
    }
}

void acpi_init(void) {
    rsdp = acpi_find_rsdp();
    if (!rsdp) return;

    acpi_parse_rsdt();
    if (!rsdt) return;

    acpi_parse_madt();

    /* Find and cache FADT */
    fadt = acpi_find_fadt();

    if (lapic_base) {
        uint32_t lapic_virt = lapic_base + VMM_KERNEL_BASE;
        vmm_map_page(0, lapic_virt, lapic_base, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        lapic_base = lapic_virt;
    }

    if (ioapic_base) {
        uint32_t ioapic_virt = ioapic_base + VMM_KERNEL_BASE;
        vmm_map_page(0, ioapic_virt, ioapic_base, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        ioapic_base = ioapic_virt;
    }
}

void lapic_write(uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(lapic_base + offset) = val;
}

uint32_t lapic_read(uint32_t offset) {
    return *(volatile uint32_t *)(lapic_base + offset);
}

void lapic_eoi(void) {
    lapic_write(0x0B0, 0);
}

void lapic_send_ipi(uint8_t dest, uint32_t cmd) {
    lapic_write(0x310, (uint32_t)dest << 24);
    lapic_write(0x300, cmd);
}

void ioapic_write(uint8_t id, uint32_t reg, uint32_t val) {
    volatile ioapic_regs_t *ioapic = (volatile ioapic_regs_t *)ioapic_base;
    ioapic->ioregsel = reg;
    ioapic->iowin = val;
}

uint32_t ioapic_read(uint8_t id, uint32_t reg) {
    volatile ioapic_regs_t *ioapic = (volatile ioapic_regs_t *)ioapic_base;
    ioapic->ioregsel = reg;
    return ioapic->iowin;
}

uint32_t acpi_get_lapic_base(void) {
    return lapic_base;
}

uint32_t acpi_get_cpu_count(void) {
    return cpu_count;
}

uint32_t acpi_get_ioapic_base(void) {
    return ioapic_base;
}

const uint8_t *acpi_get_lapic_ids(void) {
    return lapic_ids;
}

/* Find an ACPI table by 4-character signature */
void *acpi_find_table(const char *signature) {
    if (!rsdt) return 0;

    uint32_t entry_count = (rsdt->length - sizeof(rsdt_t)) / sizeof(uint32_t);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t entry_addr = rsdt->entries[i];
        uint32_t entry_virt = entry_addr;
        if (entry_virt < VMM_KERNEL_BASE) {
            entry_virt = entry_addr + VMM_KERNEL_BASE;
        }

        /* Map the page if needed */
        vmm_map_page(0, entry_virt, entry_addr, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);

        char *sig_ptr = (char *)entry_virt;
        if (memcmp(sig_ptr, signature, 4) == 0) {
            return (void *)entry_virt;
        }
    }
    return 0;
}

facp_t *acpi_find_fadt(void) {
    return (facp_t *)acpi_find_table("FACP");
}

facp_t *acpi_get_fadt(void) {
    return fadt;
}
