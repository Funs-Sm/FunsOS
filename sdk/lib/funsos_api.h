/* funsos_api.h - SDK API 公共头文件
 * 供外部应用使用的 API 声明
 */

#ifndef FUNSOS_API_H
#define FUNSOS_API_H

#include "funsos.h"

/* SDK 初始化 */
int funsos_sdk_init(void);
int funsos_sdk_cleanup(void);

/* 内存分配（SDK 层封装） */
void *funsos_alloc(uint32_t size);
void funsos_free(void *ptr);

#endif /* FUNSOS_API_H */
