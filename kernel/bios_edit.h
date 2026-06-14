#ifndef BIOS_EDIT_H
#define BIOS_EDIT_H

#include "stdint.h"

/* BIOS 固件编辑系统 - 读写真实 BIOS 数据 */

/* BIOS 数据区物理地址范围 */
#define BIOS_ROM_BASE   0xF0000
#define BIOS_ROM_END    0xFFFFF
#define BIOS_ROM_SIZE   0x10000  /* 64KB */

void bios_edit_init(void);
int bios_read_byte(uint32_t offset, uint8_t *out);
int bios_write_byte(uint32_t offset, uint8_t value);
void bios_dump(uint32_t start, uint32_t length);
int bios_edit_cmd(const char *args);  /* shell 命令入口 */

#endif
