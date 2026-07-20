#include "irqdomain.h"
#include "klog.h"
#include "string.h"

static struct irq_domain *irq_domain_list;
static struct irq_domain irq_domain_pool[IRQ_DOMAIN_MAX_DOMAINS];
static uint8_t irq_domain_used[IRQ_DOMAIN_MAX_DOMAINS];
static uint8_t irqdomain_initialized;
static uint32_t irq_domain_count;
static uint32_t next_virq;
static uint32_t total_mappings_created;
static uint32_t total_mappings_disposed;
static uint32_t total_acks;
static uint32_t total_masks;
static uint32_t total_unmasks;

int irqdomain_init(void) {
    if (irqdomain_initialized) return 0;

    irq_domain_list = NULL;
    memset(irq_domain_pool, 0, sizeof(irq_domain_pool));
    memset(irq_domain_used, 0, sizeof(irq_domain_used));

    irq_domain_count = 0;
    next_virq = 32;
    total_mappings_created = 0;
    total_mappings_disposed = 0;
    total_acks = 0;
    total_masks = 0;
    total_unmasks = 0;

    irqdomain_initialized = 1;
    klog_info("IRQ Domain manager initialized (%u domains max)", IRQ_DOMAIN_MAX_DOMAINS);
    return 0;
}

static struct irq_domain *alloc_irq_domain(void) {
    for (uint32_t i = 0; i < IRQ_DOMAIN_MAX_DOMAINS; i++) {
        if (!irq_domain_used[i]) {
            struct irq_domain *d = &irq_domain_pool[i];
            memset(d, 0, sizeof(*d));
            irq_domain_used[i] = 1;
            return d;
        }
    }
    return NULL;
}

struct irq_domain *irq_domain_create(const char *name,
                                      const struct irq_domain_ops *ops,
                                      uint32_t size,
                                      uint32_t flags,
                                      void *host_data) {
    if (!irqdomain_initialized || !name || size == 0) return NULL;
    if (size > IRQ_DOMAIN_MAP_SIZE) size = IRQ_DOMAIN_MAP_SIZE;

    struct irq_domain *d = alloc_irq_domain();
    if (!d) {
        klog_info("irqdomain: domain pool full");
        return NULL;
    }

    strncpy(d->name, name, IRQ_DOMAIN_NAME_LEN - 1);
    d->ops = ops;
    d->flags = flags | IRQ_DOMAIN_FLAG_LINEAR;
    d->size = size;
    d->base_irq = next_virq;
    d->mapped_count = 0;
    d->magic = IRQ_DOMAIN_MAGIC;
    d->host_data = host_data;

    for (uint32_t i = 0; i < IRQ_DOMAIN_MAP_SIZE; i++) {
        d->hwirq_map[i] = 0xFFFFFFFF;
        d->revmap[i] = 0xFFFFFFFF;
    }

    d->next = irq_domain_list;
    irq_domain_list = d;
    irq_domain_count++;
    next_virq += size;

    klog_info("irqdomain: created domain '%s' (size=%u, virq_base=%u)",
             name, size, d->base_irq);
    return d;
}

struct irq_domain *irq_domain_add_legacy(const char *name,
                                          const struct irq_domain_ops *ops,
                                          uint32_t size,
                                          uint32_t first_irq,
                                          void *host_data) {
    if (!irqdomain_initialized || !name || size == 0) return NULL;

    struct irq_domain *d = alloc_irq_domain();
    if (!d) return NULL;

    strncpy(d->name, name, IRQ_DOMAIN_NAME_LEN - 1);
    d->ops = ops;
    d->flags = IRQ_DOMAIN_FLAG_LEGACY;
    d->size = size;
    d->base_irq = first_irq;
    d->mapped_count = 0;
    d->magic = IRQ_DOMAIN_MAGIC;
    d->host_data = host_data;

    for (uint32_t i = 0; i < IRQ_DOMAIN_MAP_SIZE; i++) {
        d->hwirq_map[i] = 0xFFFFFFFF;
        d->revmap[i] = 0xFFFFFFFF;
    }

    for (uint32_t hwirq = 0; hwirq < size; hwirq++) {
        d->hwirq_map[hwirq] = first_irq + hwirq;
        if (first_irq + hwirq < IRQ_DOMAIN_MAP_SIZE) {
            d->revmap[first_irq + hwirq] = hwirq;
        }
        d->mapped_count++;
        total_mappings_created++;
    }

    d->next = irq_domain_list;
    irq_domain_list = d;
    irq_domain_count++;

    if (first_irq + size > next_virq) {
        next_virq = first_irq + size;
    }

    klog_info("irqdomain: added legacy domain '%s' (size=%u, first_irq=%u)",
             name, size, first_irq);
    return d;
}

irq_virq_number_t irq_create_mapping(struct irq_domain *domain,
                                      irq_hw_number_t hwirq) {
    if (!irqdomain_initialized || !domain || hwirq >= domain->size) return 0xFFFFFFFF;

    if (domain->hwirq_map[hwirq] != 0xFFFFFFFF) {
        return domain->hwirq_map[hwirq];
    }

    irq_virq_number_t virq = next_virq++;
    if (virq >= IRQ_DOMAIN_MAP_SIZE) return 0xFFFFFFFF;

    domain->hwirq_map[hwirq] = virq;
    domain->revmap[virq] = hwirq;
    domain->mapped_count++;
    total_mappings_created++;

    if (domain->ops && domain->ops->map) {
        domain->ops->map(domain, virq, hwirq);
    }

    return virq;
}

void irq_dispose_mapping(irq_virq_number_t virq) {
    if (!irqdomain_initialized || virq == 0xFFFFFFFF) return;

    struct irq_domain *d = irq_domain_list;
    while (d) {
        if (d->magic == IRQ_DOMAIN_MAGIC && virq < IRQ_DOMAIN_MAP_SIZE) {
            irq_hw_number_t hwirq = d->revmap[virq];
            if (hwirq != 0xFFFFFFFF) {
                if (d->ops && d->ops->unmap) {
                    d->ops->unmap(d, virq);
                }
                d->hwirq_map[hwirq] = 0xFFFFFFFF;
                d->revmap[virq] = 0xFFFFFFFF;
                if (d->mapped_count > 0) d->mapped_count--;
                total_mappings_disposed++;
                return;
            }
        }
        d = d->next;
    }
}

irq_virq_number_t irq_find_mapping(struct irq_domain *domain,
                                    irq_hw_number_t hwirq) {
    if (!irqdomain_initialized || !domain || hwirq >= IRQ_DOMAIN_MAP_SIZE) return 0xFFFFFFFF;
    return domain->hwirq_map[hwirq];
}

irq_hw_number_t irq_find_hwirq(struct irq_domain *domain,
                                irq_virq_number_t virq) {
    if (!irqdomain_initialized || !domain || virq >= IRQ_DOMAIN_MAP_SIZE) return 0xFFFFFFFF;
    return domain->revmap[virq];
}

struct irq_domain *irq_domain_lookup(const char *name) {
    if (!irqdomain_initialized || !name) return NULL;

    struct irq_domain *d = irq_domain_list;
    while (d) {
        if (d->magic == IRQ_DOMAIN_MAGIC && strcmp(d->name, name) == 0) {
            return d;
        }
        d = d->next;
    }
    return NULL;
}

void irq_domain_ack_irq(struct irq_domain *domain, irq_hw_number_t hwirq) {
    if (!irqdomain_initialized || !domain) return;
    total_acks++;
    if (domain->ops && domain->ops->ack) {
        domain->ops->ack(domain, hwirq);
    }
}

void irq_domain_mask_irq(struct irq_domain *domain, irq_hw_number_t hwirq) {
    if (!irqdomain_initialized || !domain) return;
    total_masks++;
    if (domain->ops && domain->ops->mask) {
        domain->ops->mask(domain, hwirq);
    }
}

void irq_domain_unmask_irq(struct irq_domain *domain, irq_hw_number_t hwirq) {
    if (!irqdomain_initialized || !domain) return;
    total_unmasks++;
    if (domain->ops && domain->ops->unmask) {
        domain->ops->unmask(domain, hwirq);
    }
}

void irqdomain_print_stats(void) {
    klog_info("=== IRQ Domain Manager Statistics ===");
    klog_info("Registered domains: %u / %u", irq_domain_count, IRQ_DOMAIN_MAX_DOMAINS);
    klog_info("Next virtual IRQ: %u", next_virq);
    klog_info("Total mappings created: %u", total_mappings_created);
    klog_info("Total mappings disposed: %u", total_mappings_disposed);
    klog_info("IRQ acks: %u", total_acks);
    klog_info("IRQ masks: %u", total_masks);
    klog_info("IRQ unmasks: %u", total_unmasks);

    klog_info("Active IRQ domains:");
    struct irq_domain *d = irq_domain_list;
    int i = 0;
    while (d) {
        if (d->magic == IRQ_DOMAIN_MAGIC) {
            const char *type = "linear";
            if (d->flags & IRQ_DOMAIN_FLAG_LEGACY) type = "legacy";

            klog_info("  [%d] name='%s' type=%s size=%u base_virq=%u mapped=%u",
                     i++, d->name, type, d->size, d->base_irq, d->mapped_count);

            klog_info("       mappings (hwirq -> virq):");
            int shown = 0;
            for (uint32_t hwirq = 0; hwirq < d->size && shown < 8; hwirq++) {
                if (d->hwirq_map[hwirq] != 0xFFFFFFFF) {
                    klog_info("         %u -> %u", hwirq, d->hwirq_map[hwirq]);
                    shown++;
                }
            }
            if (d->mapped_count > 8) {
                klog_info("         ... (%u more)", d->mapped_count - 8);
            }
        }
        d = d->next;
    }
}
