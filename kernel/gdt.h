#ifndef GDT_H
#define GDT_H

#include "stdint.h"

void init_gdt(void);
void gdt_set_tss(uint32_t ss0, uint32_t esp0);
void flush_tss(void);

#endif
