#ifndef IRQDOMAIN_H
#define IRQDOMAIN_H

#include "stdint.h"

#define IRQ_DOMAIN_MAX_DOMAINS    16
#define IRQ_DOMAIN_MAP_SIZE       256
#define IRQ_DOMAIN_NAME_LEN       32
#define IRQ_DOMAIN_MAGIC          0x49525144

#define IRQ_DOMAIN_FLAG_LEGACY    0x0001
#define IRQ_DOMAIN_FLAG_NOMAP     0x0002
#define IRQ_DOMAIN_FLAG_LINEAR    0x0004

typedef uint32_t irq_hw_number_t;
typedef uint32_t irq_virq_number_t;

struct irq_domain;

typedef int (*irq_map_func_t)(struct irq_domain *d, irq_virq_number_t virq,
                               irq_hw_number_t hwirq);
typedef int (*irq_unmap_func_t)(struct irq_domain *d, irq_virq_number_t virq);
typedef void (*irq_ack_func_t)(struct irq_domain *d, irq_hw_number_t hwirq);
typedef void (*irq_mask_func_t)(struct irq_domain *d, irq_hw_number_t hwirq);
typedef void (*irq_unmask_func_t)(struct irq_domain *d, irq_hw_number_t hwirq);

struct irq_domain_ops {
    irq_map_func_t map;
    irq_unmap_func_t unmap;
    irq_ack_func_t ack;
    irq_mask_func_t mask;
    irq_unmask_func_t unmask;
};

struct irq_domain {
    char name[IRQ_DOMAIN_NAME_LEN];
    struct irq_domain *next;
    const struct irq_domain_ops *ops;
    uint32_t flags;
    uint32_t size;
    uint32_t base_irq;
    irq_hw_number_t hwirq_map[IRQ_DOMAIN_MAP_SIZE];
    irq_virq_number_t revmap[IRQ_DOMAIN_MAP_SIZE];
    uint32_t mapped_count;
    uint32_t magic;
    void *host_data;
};

int irqdomain_init(void);

struct irq_domain *irq_domain_create(const char *name,
                                      const struct irq_domain_ops *ops,
                                      uint32_t size,
                                      uint32_t flags,
                                      void *host_data);

struct irq_domain *irq_domain_add_legacy(const char *name,
                                          const struct irq_domain_ops *ops,
                                          uint32_t size,
                                          uint32_t first_irq,
                                          void *host_data);

irq_virq_number_t irq_create_mapping(struct irq_domain *domain,
                                      irq_hw_number_t hwirq);

void irq_dispose_mapping(irq_virq_number_t virq);

irq_virq_number_t irq_find_mapping(struct irq_domain *domain,
                                    irq_hw_number_t hwirq);

irq_hw_number_t irq_find_hwirq(struct irq_domain *domain,
                                irq_virq_number_t virq);

struct irq_domain *irq_domain_lookup(const char *name);

void irq_domain_ack_irq(struct irq_domain *domain, irq_hw_number_t hwirq);
void irq_domain_mask_irq(struct irq_domain *domain, irq_hw_number_t hwirq);
void irq_domain_unmask_irq(struct irq_domain *domain, irq_hw_number_t hwirq);

void irqdomain_print_stats(void);

#endif
