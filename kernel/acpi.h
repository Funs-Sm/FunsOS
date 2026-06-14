#ifndef ACPI_H
#define ACPI_H

#include "stdint.h"

#define LAPIC_BASE_PHYS 0xFEE00000

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) rsdp_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t entries[];
} __attribute__((packed)) rsdt_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed)) madt_t;

typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) madt_entry_lapic_t;

typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed)) madt_entry_ioapic_t;

typedef struct {
    uint32_t lapic_id;
    uint32_t lapic_version;
    uint32_t reserved0[4];
    uint32_t tpr;
    uint32_t reserved1[3];
    uint32_t eoi;
    uint32_t reserved2;
    uint32_t logical_dest;
    uint32_t reserved3;
    uint32_t spurious;
    uint32_t reserved4[5];
    uint32_t icr_low;
    uint32_t reserved5;
    uint32_t icr_high;
    uint32_t reserved6[3];
    uint32_t lvt_timer;
    uint32_t reserved7[3];
    uint32_t lvt_lint0;
    uint32_t reserved8;
    uint32_t lvt_lint1;
    uint32_t reserved9;
    uint32_t error;
    uint32_t reserved10[7];
    uint32_t timer_initial;
    uint32_t timer_current;
    uint32_t timer_divide;
} __attribute__((packed)) lapic_regs_t;

typedef struct {
    uint32_t ioregsel;
    uint32_t iowin;
} __attribute__((packed)) ioapic_regs_t;

/* FACP (Fixed ACPI Description Table) */
typedef struct {
    char signature[4];         /* "FACP" */
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t firmware_ctrl;    /* FACS address */
    uint32_t dsdt;             /* DSDT address */
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;          /* SCI interrupt vector */
    uint32_t smi_cmd;          /* SMI command port */
    uint8_t acpi_enable;       /* Value to write to smi_cmd to enable ACPI */
    uint8_t acpi_disable;      /* Value to write to smi_cmd to disable ACPI */
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;     /* PM1a Event Register Block */
    uint32_t pm1b_evt_blk;     /* PM1b Event Register Block */
    uint32_t pm1a_cnt_blk;     /* PM1a Control Register Block */
    uint32_t pm1b_cnt_blk;     /* PM1b Control Register Block */
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;       /* PM Timer Register Block */
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;       /* C2 latency */
    uint16_t p_lvl3_lat;       /* C3 latency */
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    /* Reset register (ACPI 2.0+) */
    uint8_t reset_reg[12];     /* Generic Address Structure */
    uint8_t reset_value;
    uint8_t reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    uint8_t x_pm1a_evt_blk[12];
    uint8_t x_pm1b_evt_blk[12];
    uint8_t x_pm1a_cnt_blk[12];
    uint8_t x_pm1b_cnt_blk[12];
    uint8_t x_pm2_cnt_blk[12];
    uint8_t x_pm_tmr_blk[12];
    uint8_t x_gpe0_blk[12];
    uint8_t x_gpe1_blk[12];
} __attribute__((packed)) facp_t;

/* FACS (Firmware ACPI Control Structure) */
typedef struct {
    char signature[4];         /* "FACS" */
    uint32_t length;
    uint32_t hardware_signature;
    uint32_t firmware_waking_vector;
    uint32_t global_lock;
    uint32_t flags;
    uint64_t x_firmware_waking_vector;
    uint8_t version;
    uint8_t reserved[31];
} __attribute__((packed)) facs_t;

void acpi_init(void);
rsdp_t *acpi_find_rsdp(void);
void acpi_parse_rsdt(void);
void acpi_parse_madt(void);
uint32_t acpi_get_lapic_base(void);
uint32_t acpi_get_cpu_count(void);
uint32_t acpi_get_ioapic_base(void);
const uint8_t *acpi_get_lapic_ids(void);
void lapic_write(uint32_t offset, uint32_t val);
uint32_t lapic_read(uint32_t offset);
void ioapic_write(uint8_t id, uint32_t reg, uint32_t val);
uint32_t ioapic_read(uint8_t id, uint32_t reg);
void lapic_eoi(void);
void lapic_send_ipi(uint8_t dest, uint32_t cmd);

/* FACP access */
facp_t *acpi_find_fadt(void);
facp_t *acpi_get_fadt(void);

/* Generic ACPI table lookup by signature */
void *acpi_find_table(const char *signature);

#endif
