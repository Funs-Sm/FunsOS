/*
 * kernel/cmd_mem.h - PR-4 iter2+iter3
 * cmd_mem 模块对外接口
 */
#ifndef _KERNEL_CMD_MEM_H
#define _KERNEL_CMD_MEM_H

void cmd_free(void);      /* PR-4 iter2 */
void cmd_meminfo(void);   /* PR-4 iter3 */

void cmd_mem_module_init(void);

#endif