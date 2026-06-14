#ifndef CURSOR_H
#define CURSOR_H

#include "gfx.h"
#include "stdint.h"

#define CURSOR_WIDTH   12
#define CURSOR_HEIGHT  20

#define CURSOR_ARROW   0
#define CURSOR_IBEAM   1
#define CURSOR_CROSS   2
#define CURSOR_HAND    3

void cursor_init(void);
void cursor_draw(gfx_context_t *ctx, int32_t x, int32_t y);
void cursor_set_type(uint32_t type);
uint32_t cursor_get_type(void);

#endif
